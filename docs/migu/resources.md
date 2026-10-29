# Migu 资源链路

修改资源 workflow、provider/parser/fetcher、blob store、资源 API、pacmtoken 或 `song_resources` 状态回写前读取本文。涉及 SQL、认领和持久化时同时读取 [元数据约束](metadata.md)。事件字段以 [资源批次事件规范](../observability/resource-batch-events.md) 为准。

## Migu 资源下载链路维护上下文

本文记录 Migu 歌曲资源下载链路的维护约束。历史实测见 [当前交接](../agents/current.md)，回归要求见 [验证文档](../agents/validation.md)。修改 `SongResourceDownloadWorkflow`、`AsyncSongResourceDownloadWorkflow`、资源 provider/parser/fetcher/blob store、`/api/migu/resources/process`、pacmtoken 或 `song_resources` 状态回写时必须先阅读本文。

### 当前主链路

- 资源处理入口是 `POST /api/migu/resources/process`；长时间任务使用 `async=true`，返回 `202 + batch_id`，前端通过 `/api/migu/tasks`、`/api/migu/tasks/{batch_id}/items`、`/api/migu/tasks/{batch_id}/events` 查询进度。
- 资源处理流程固定为：从 MySQL `song_resources` 认领 `status IN (0, 3)` 的目标 -> audio 调用 Migu listen 接口换取临时 URL -> parser 只解析 `url/version/audioFormatType/lrcUrl/mrcUrl/trcUrl` 和失败 `cannotCode/dialogInfo.text` -> 流式下载 -> blob store 完整提交 -> 成功后回写 `local_path/checksum/size/status=2`。
- 下载成功前不得回写完成状态。blob store 必须保持 staging/commit 语义；本地文件当前用临时文件、校验大小和 checksum 后提交，后续 rclone/API 存储也必须保持“完整对象提交后才回写 DB”的语义。
- 当前主流程不再自动上传百度网盘，也不在资源下载 workflow 中调用 BaiduPCS-Go、rclone 或其它云端 uploader。后续除非用户明确要求重新做上传实验，不要把 BaiduPCS-Go 上传重新接回主流程。历史实验、本机路径与对账步骤见 [运行操作](../operations/resource-storage.md)。
- 资源相对路径约定为 `migu/songs/{contentId}/audio/{format}.{ext}`、`migu/songs/{contentId}/lyrics/lyric.lrc`、`migu/songs/{contentId}/lyrics/mrc.mrc`、`migu/songs/{contentId}/lyrics/trc.trc`。不要改用短 `song_id`，`contentId` 更稳定且与平台资源接口一致。
- 配置 `CRAWLER_MIGU_MRC_KEY` 后，本地 MRC 下载及数据库确认成功会触发独立后处理，生成同目录的 `words.json` 和 `words.enhanced.lrc`；产物元信息写入独立索引表，处理失败不撤销原始下载成功。配置、存储及离线使用见 [逐字歌词处理](lyrics.md)。
- 同一首歌的 `lyric/mrc/trc` 只处理一次。并发场景下必须保留去重集合保护，避免每个音质重复下载歌词。关联 URL 存在但队列无可认领资源时返回 synthetic skipped item：`resource_id=0`、`related=true`、`synthetic=true`、`result_code=related_not_claimed`。
- 同步 workflow 保留小批量兼容；异步 workflow 是正式长跑入口。异步资源管线使用 application 自定义 `Task<T>`，application 层不得直接 include Boost.Asio；Asio 只能出现在 infrastructure async runtime、HTTP client、MySQL adapter 等基础设施实现中。

### Migu 业务码和状态不变量

- `440000` 表示“该歌曲暂无版权”，必须视为永久跳过，整首歌未完成资源写 `status=4`，`error_category=copyright`，`result_code=440000`。
- `440015` 表示数字专辑购买限制，必须视为永久跳过，整首歌未完成资源写 `status=4`，`error_category=resource`，`result_code=440015`。
- `440016` 表示数字专辑购买限制，必须视为永久跳过，整首歌未完成资源写 `status=4`，`error_category=resource`，`result_code=440016`。
- `440013` 属于登录态、账号状态或 token 相关限制；默认在 `refresh_pacmtoken_on_440013=true` 时刷新 pacmtoken 后额外重试一次。刷新后仍失败时保留为失败资源，不得写成永久跳过。
- `status=4` 是永久跳过状态，普通认领、retry、reset、recover-stale 不得把它拉回 `0`。当前 SQL 已通过 `sr.status <> 4` 和默认认领 `status IN (0, 3)` 保护，后续修改 SQL 时必须保留。
- `markSkipped` 针对这些永久限制会清空 `resource_kind/format_type` 条件，目的是跳过同一首歌下所有未完成资源，避免不同音质重复请求。已完成的 `status=2` 不应被改写。

### pacmtoken 和敏感信息

- 项目没有 Migu 登录态生成能力。`pacmtoken` 由前端从浏览器环境取得后调用 `POST /api/migu/pacmtoken/refresh` 写入后端运行期内存；后端不会把 token 写入 `.env`、MySQL 批次表、事件或日志。
- `POST /api/migu/pacmtoken/current` 只供前端显式同步本地 token；不要自动高频调用，不要把响应体写日志或埋点。
- 批次启动和长跑中可按 `pacmtoken_refresh_interval_seconds` 自动刷新，事件只允许记录 `pacmtokenRefreshed`、长度和时间，禁止记录 token 明文。
- 禁止在日志、批次 payload、测试输出、文档示例中打印 Cookie、pacmtoken、完整 listen URL、完整临时下载 URL、签名参数或 `Key/Tim/playSessionId`。

### 并发、重试和运行配置

- 前端/生产默认资源并发建议从 `3` 或 `5` 开始，`resource_download_concurrency` 范围 `1..32`。历史探测结果见 [当前交接](../agents/current.md)，不能假设平台永不限流。
- 只对 `network_failed`、`timeout`、HTTP `5xx`、listen URL 获取网络失败这类可恢复错误重试。音频重试必须重新请求 listen URL，不能复用旧临时 URL。
- 不重试数字专辑、暂无版权、参数缺失、解析结构错误、持久化失败、`440000/440015/440016`。
- `resource_download_retries` 范围 `0..5`，`resource_download_timeout_ms` 范围 `10000..600000`，`pacmtoken_refresh_interval_seconds` 范围 `0..3600`，`0` 表示关闭长跑定时刷新。

### 前端和 API 对接注意事项

- 前端最小闭环：刷新 pacmtoken -> 选择 `format/limit/concurrency` -> `POST /api/migu/resources/process` 且 `async=true` -> 保存 `batch_id` -> 轮询 tasks/items/events。
- 下载前可用 `GET /api/migu/resources/queue?status=0,3&kind=audio&format=PQ` 展示待处理量。`status=4` 资源不要展示普通 retry/reset 操作。
- 资源 process 请求示例：

```json
{
  "source": "migu",
  "limit": 50,
  "async": true,
  "filter": {
    "kind": "audio",
    "format": "PQ",
    "status": [0, 3]
  },
  "options": {
    "resource_download_concurrency": 3,
    "resource_download_retries": 1,
    "resource_download_timeout_ms": 120000,
    "pacmtoken_refresh_interval_seconds": 300,
    "auto_refresh_pacmtoken": true,
    "refresh_pacmtoken_on_440013": true
  }
}
```

- 前端展示时把 `440000/440015/440016` 归为“永久跳过”，不要作为需要重试的失败告警；把 `440013` 展示为 token/登录态可能失效，引导用户刷新 pacmtoken 后再 retry。
- `related_not_claimed` 是 synthetic 关联资源跳过明细，不代表主资源下载失败，前端可折叠展示。

## 关联文档

歌手和专辑封面另见下方独立流程；歌曲的 listen、pacmtoken 和永久跳过规则继续按本文前述约束执行。

- [验证要求](../agents/validation.md)：同步和异步资源回归、API 检查与真实测试条件。
- [运行操作](../operations/resource-storage.md)：手动上传、云端对账和本机排障。
- [API 说明](../api/mg/migu-api.md) 与 [OpenAPI](../api/mg/openapi-migu.yaml)：完整协议；已知文档差异见 [当前交接](../agents/current.md)。

## 歌手头像与专辑封面本地下载

- `GET /api/migu/covers/queue` 查询各自资源表，`POST /api/migu/covers/process` 下载已入库的 `remote_url`；该流程不调用歌曲 listen 或为图片重新获取歌手/专辑详情。当前生产装配支持 MySQL 与本地文件存储，SQLite 兼容装配返回未配置。
- 请求必须指定 `owner_type=singer|album`；资源身份是所有者类型与 `resource_id`，两张表的相同数字 ID 不相互替代。默认认领 `status=0,3`，完整文件与 DB 回执确认后状态为 2，认领期间为 1。
- 下载在短认领事务之外执行，回写比较认领代次与 URL 快照；目录重采更新 URL 后，旧请求不能把旧图标成新地址的结果。`claimed_at` 独立于元数据更新时间，`/covers/recover-stale` 使超时代次失效。
- 使用既有存储根，目录为 `migu/singers/{平台歌手ID}/cover/`、`migu/albums/{规范专辑ID}/cover/`，实际文件名包含独占后缀。单图上限 8 MiB，默认总超时 15 秒，最多三次 HTTP 跳转；按内容判断格式，并校验传输、大小与 SHA-256。图片读取重新核对 checksum。
- HTTPS 保持证书链和主机名校验，Windows OpenSSL 客户端同时装载系统 ROOT。CDN 自动返回 WebP 时按实际格式保存与响应，HTTP adapter 使用自定义 Content-Type 保留图片 MIME。
- 已完成资源默认复用；`refresh=true` 需要显式 `resource_ids`，用于换图或修复本地文件丢失。刷新过程与失败均保留旧成功文件回执，`available` 与当前任务 `status` 分别表达可用性和下载状态。
- queue/catalog 的 `available` 只做根边界、存在性和大小检查；content 和实际复用额外验证图片及 SHA-256。同大小损坏或文件缺失不能算成功跳过，process 计失败并保留旧 DB 回执，显式 refresh 用于修复。默认队列不遍历已完成资源做全盘校验。
- DB 明确拒绝时清理本次独占文件；无法确认提交时返回 `writeback_uncertain`，保留新文件和日志。文件日志携带 `song|singer|album` 类型，恢复时检查正确资源表；专辑别名归一迁移后的回执仍受引用保护。
- 写入响应报错后的普通重读只能确认已看到的匹配成功，不能用未命中或不同快照证明未提交；未证实的结果继续保留未知。条件失败及成功刷新后重读当前记录，操作错误与持久化文件快照分别表达，旧 worker 不返回已经过期的云端状态或文件回执。
- 长任务默认 `async=true`，返回 `202 + batch_id`，进度、明细、事件和取消复用现有任务路由。同步模式顺序处理有界小批次；`concurrency` 控制后台 worker 数量，范围 `1..32`。
- 歌手/专辑列表和详情的 `cover` 对象返回本地回执与 `local_url`，原 `avatar_url/cover_url` 仍为平台地址。内容路由按资源身份解析已提交文件，支持 MIME、ETag 和 304；未知、缺失或损坏文件返回 404，不接受用户指定路径。
- 开启 `CRAWLER_RESOURCE_CLOUD_UPLOAD_ENABLED` 后，同一 process 在本地成功后调用共用云端用例。配置后端 `baidupcs` 或 `rclone`，分别接百度与 Google；已有本地文件仅补传。远端内容核验通过后独立认领、条件回写 `cloud_*`，失败保留本地 `status=2` 和图片，未知状态可重新核对。云端回执、认领和错误独立于本地状态，刷新和专辑别名归一需与本地文件成套处理。
- 本地实现与验收见[当前交接](../agents/current.md)及[本地专项](../roadmap/metadata-cover-download.md)，共用云端实现与验收见[云端存储](../roadmap/metadata-cover-cloud-storage.md)。歌曲上传装配另行维护。
