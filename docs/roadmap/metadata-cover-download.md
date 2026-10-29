# 歌手头像与专辑封面本地下载

更新日期：2026-10-29。源码核对基线：`21b19a5`。当前状态：本地实现完成，40 张真实图片、离线/本机与独立 MySQL 回归通过；并行歌手字段清理后的目录联调待统一合并。

本专项记录已完成的本地阶段。用户在 2026-10-29 追加要求开启云端后自动上传并回写，接续方案见[云端接入规划](metadata-cover-cloud-storage.md)，不再沿用此前的“云端延后”作为当前任务。本专项承接[总路线的元数据剩余项](README.md#元数据专项剩余项)，执行与验证遵循[任务分类指南](../agents/task-routing.md)和[验证要求](../agents/validation.md)。

## 目标与边界

从已经入库的歌手头像、专辑封面地址发起独立任务，将图片可靠保存到本地，并让队列和歌手/专辑查询返回可访问的本地封面信息。成功标准包含正确归属、完整文件、可靠回写、重复执行复用和失败可恢复。

- 元数据采集继续只登记 URL；下载使用独立入口与 workflow，网络操作在数据库事务外执行。
- 歌手资源归 `singer_resources`，专辑资源归 `album_resources`，使用各自来源主键识别所有者；公共名称池 ID 和两个资源表中碰巧相同的数字 ID 不能互换。
- 歌曲资源继续使用原 `/api/migu/resources/process`；共享 HTTP、文件提交或恢复组件实际受影响时联动歌曲回归。
- 沿用现有本地存储根与 `migu/singers/{source_singer_id}/cover/`、`migu/albums/{source_album_id}/cover/` 目录，记录 adapter 返回的实际相对路径。
- 本地阶段交付下载、查询与恢复；云端配置、自动上传、核验及云端字段由[接续专项](metadata-cover-cloud-storage.md)维护。

## 实施前核对的基线

| 事实 | 代码或资料依据 | 实施影响 |
| --- | --- | --- |
| 图片 URL 已随元数据同事务保存；更新 URL 保留已有成功文件 | [歌手 URL SQL](../../sql/mysql/singer_resources/upsert_remote_url.sql)、[专辑 URL SQL](../../sql/mysql/album_resources/upsert_remote_url.sql)、[现有测试](../../tests/migu/integration/MySqlMetadataCoverUrlTest.cpp) | 独立下载读取资源表；重采元数据不触发下载 |
| 两类表已有 URL、本地回执、状态和错误，尚无独立下载认领代次及运行批次 | [歌手结构](../../sql/mysql/singer_resources/create.sql)、[专辑结构](../../sql/mysql/album_resources/create.sql) | 新增必要的任务状态、条件回写与恢复能力，保留已有数据 |
| 封面保存器限制 15 秒、8 MiB，检查图片签名，支持文件提交及补偿；当前无独立下载 workflow 和生产调用 | [MetadataCoverStore](../../src/infrastructure/storage/filesystem/MetadataCoverStore.cpp) | 优先复用并接入独立编排 |
| 保存器将无响应、异常及非 2xx 统一归为 `cover_fetch_failed`；HTTP 单次请求返回当前响应 | [保存器](../../src/infrastructure/storage/filesystem/MetadataCoverStore.cpp)、[HTTP 请求](../../include/crawler/infrastructure/http/client/AsioHttpCoroutineRequest.h) | 补可诊断分类；对跳转、超时、错误页和中断建立样本，目前无真实失败根因结论 |
| 歌手/专辑查询返回平台 URL，尚无本地图片读取路由 | [目录模型](../../include/crawler/application/model/MusicCatalog.h)、[控制器](../../src/api/controllers/CrawlerController.cpp)、[路由](../../src/api/routing/CrawlerRoutes.cpp) | 增加本地封面状态与受控读取，验收包含接口可用性 |
| 歌曲图片仍可补空专辑 URL，原始字段可能属于歌曲图片 | [批次构建](../../src/application/use_cases/MetadataBatchBuilder.cpp)、[歌曲解析](../../src/infrastructure/music/migu/parser/MiguSongSourceParsing.cpp) | 用明确专辑上下文约束新输入；历史 URL 按证据处理 |
| 原封面日志用 `resource_id=0` 查询两类表的已提交文件，缺少运行中封面代次判断 | [回执恢复](../../src/infrastructure/music/persistence/mysql/MySqlResourceWriteRecovery.cpp) | 补所有者与代次识别，避免混用歌曲 ID 或回收活跃文件 |

以上为实施前源码阅读结果，对应缺口已按下方步骤补齐；历史图片下载成功记录不计作本专项实测。

## 实施顺序与验收

1. **核对来源与复现问题。** 用脱敏 fixture 覆盖歌手索引/资料 `img`，专辑列表 `img`、详情/搜索 `imgItem/imgItems`，验证相对地址与协议相对地址归一化。区分专辑图片与普通歌曲图片，只修有证据的归属错误；缺图保留缺失，不增加逐字段补查。建立正常图片、跳转、4xx/5xx、超时、200 错误页及传输中断样本，实际平台异常另行取证。
2. **补独立封面队列。** application 依赖小型封面队列 port，MySQL adapter 读写现有两张资源表，所有者类型使用受限枚举。新增 migration、表级 SQL、结构快照和 schema 检查，补条件认领、代次、批次及恢复字段。默认处理待获取/失败项；同一资源只允许一个有效认领，旧代次或旧 URL 快照不能覆盖新结果，网络请求不持有数据库锁。
3. **接本地下载编排。** 复用封面抓取、图片签名与 `FileStorage` 会话，用有界执行器隔离阻塞操作。设置超时和大小上限，核对传输完整性、实际字节数和 checksum；完整提交后再条件回写。区分网络、超时、HTTP、内容及持久化错误，只对可恢复错误有界重试；如需跳转支持则限制次数并检测循环。错误文本脱敏。
4. **保证复用与恢复。** 成功且文件有效的同一所有者跨批次复用，显式 `refresh` 用于指定成功项重取或文件修复。任务状态与上次成功文件回执分别表达，刷新期间及刷新失败后保留旧文件。DB 明确拒绝时清理本次文件，提交不确定时保留文件与日志；成功确认后才替换回执、清理无引用旧对象。取消停止后续认领，异常退出后通过 stale 恢复重新认领。
5. **接 HTTP 与目录查询。** 按下方契约装配独立 workflow，复用批次查询、取消和事件设施，记录封面所有者。控制器仅转换协议，本地读取通过基础设施 adapter。同步静态 OpenAPI、运行期 OpenAPI、API 和必要事件说明。
6. **分层验收与更新路线。** 先离线 fixture 和本机 HTTP/临时文件，再以有效独立 MySQL 配置验证 migration、认领与回写。最后在已有授权及可用配置范围内做小批量真实图片下载，核对文件、大小、checksum、本地查询地址及重复执行请求数。未执行的层级单独标明。

共享抓取和保存逻辑用于消除重复，所有者策略保留在窄 port 与 adapter 中。本期不预建通用下载平台或全局内容寻址库。

## 接口与实现边界

以下路由已接入，实现契约见 API 说明及两份 OpenAPI。`owner_type` 取 `singer` 或 `album`，`resource_id` 必须与类型一起使用。

| 接口 | 用途与约束 |
| --- | --- |
| `GET /api/migu/covers/queue` | 按所有者类型、来源、资源 ID、状态分页，返回本地可用性与稳定错误 |
| `POST /api/migu/covers/process` | 按库中资源 ID 或受限筛选启动下载；支持 `async=true`、limit、有界并发/重试和显式 `refresh`；不接受任意外部 URL 或本地路径 |
| `POST /api/migu/covers/recover-stale` | 恢复超时认领，保护运行中的新代次；失败资源可直接由 process 重认领，暂不复制整套歌曲治理接口 |
| `GET /api/migu/covers/{owner_type}/{resource_id}/content` | 从资源归属定位已提交文件，返回实际 MIME、缓存校验信息与图片字节；文件不存在返回 404，调用方不能传文件路径 |

长任务返回 `202 + batch_id`，进度、明细、事件和取消复用既有 `/api/migu/tasks` 路由族，使用明确的封面 item 类型和所有者字段。

歌手/专辑列表及详情增加本地封面信息，例如 `cover.resource_id/status/available/local_url/size_bytes/checksum`；已有 `avatar_url`、`cover_url` 继续表示平台远端地址。本地 URL 由 API 根据所有者和资源 ID 生成，文件读取 port 验证已提交回执及文件存在性；绝对存储根不进入响应。

URL 更新默认保留成功文件。下载期间 URL 改变时，条件回写应拒绝过期快照，避免将旧图片标成新地址的结果。历史来源不明的专辑 URL 单列证据，优先复用已有详情响应，不自动批量清理或扩采公共归档。

## 验证计划与完成标准

| 层级 | 验证内容 | 入口与条件 |
| --- | --- | --- |
| 离线与本机 I/O | 两类所有者、图片类型、错误页、跳转边界、超限、中断、重试、重复执行、刷新失败保旧、取消和回写不确定 | 已注册 `MetadataCoverDownloadWorkflowTest`、`MetadataCoverStoreTest` |
| 元数据边界 | 采集不下载、真实图片归属、详情 URL 优先、重采保成功文件、专辑别名归一后归属有效 | 按影响运行已有 `MetadataOwnershipTest`、相关 parser、`MySqlMetadataCoverUrlTest`、`MySqlAlbumIdentityTest`；后两项使用独立 MySQL |
| 独立 MySQL | 历史升级与重跑、并发认领、旧代次拒绝、URL 竞态、失败/stale 恢复、回执与 prepare | 已注册 `MySqlMetadataCoverQueueTest`；运行前核对独立 `crawler_ownership_test_*` 配置 |
| API 与文件恢复 | 真路由、参数、两表 ID 隔离、异步查询、本地 MIME/404/缓存、OpenAPI 和活跃文件保留 | `MetadataCoverApiTest`、受影响的 `MiguCatalogApiSmokeTest`、`MiguHttpRouteSmokeTest`、`MiguApiDocumentTest` 及文件恢复测试 |
| 小批量真实图片 | 选定歌手/专辑的下载、回写、本地访问、第二轮复用及原失败样本回归 | 手动 target `MiguMetadataCoverDownloadProbe` 或克隆库实际服务；本次通过真实 HTTP 验证 20 张歌手图、20 张专辑图和不受信任 TLS 反例 |

最低标准：成功记录对应完整可读文件；两类所有者与本地路由匹配；第二轮默认处理不重复请求成功图片；失败不伪造成功；刷新失败不损坏旧图；旧 worker 不能覆盖新代次；受影响回归实际通过。构建、静态检查、离线、数据库和真实图片结果分别记录。

## 本次验收结果

2026-10-28 至 29 在 `codex/artist-album-cover-storage` 工作区验证，未提交；证据根为 `D:/codex-runs/crawler-cover-local/20261028/`。

- **构建：** MSVC/vcpkg 的 `demo` 及受影响测试 target 构建成功。有效目录为 `D:/codex-builds/crawler-cover-local-20261028`，C 盘空间不足后仅在本地 cache 关闭可执行文件链接调试符号和增量链接，未改变默认 preset。
- **离线与本机：** 初始实现 15 项通过，见 `offline_final.txt`。补 Windows CA、MIME、schema 和契约后，受影响的 8 项再次通过，见 `api-ca-mime-final-tests.txt`；含 WebP/AVIF 自定义 MIME 真路由、封面存储/API、歌曲同步/异步下载及流式 HTTP。
- **独立 MySQL：** `MySqlMetadataCoverQueueTest`、`MySqlMetadataCoverUrlTest`、`MySqlMetadataOwnershipTest`、`MySqlAlbumIdentityTest` 分别在 `V2026102802` 的四个新库通过；覆盖 schema 状态、升级/重跑、并发认领、代次与 URL 竞态、别名归一、回执、回滚及 174 份运行期 SQL prepare。证据在 `mysql-v2802-20261029_001341/`，完整库名见[当前交接](../agents/current.md#构建与证据)。
- **契约与静态检查：** 顶部 API 清单、推荐流程及静态/运行期 OpenAPI `1.8.0` 对齐；4 个新增路径、6 个 schema 和 2 个目录字段一致，迁移 checksum 匹配，见 `contract-check.txt`。初始 19 个及本轮 4 个实现的 clangd 编译/preamble/AST 阶段无诊断；部分命令因编辑器重构 tweak 自检失败而非零退出，不能计为全部 clangd 检查通过。
- **真实平台：** 用户指定 `crawler_ownership_test_main20_20261028_154114`，以单事务快照克隆为 `crawler_ownership_test_cover_main20_20261028_235106`。22 张表、5 个视图、数据哈希一致且引用留在克隆内；本会话未写源库。20 张歌手图、20 张专辑图共 4,812,571 字节通过实际 HTTP 下载、回执、文件 SHA-256、本地 MIME、ETag/304、第二轮复用、任务/事件和错误接口验收；图片包含 WebP/JPEG/PNG。TLS 校验保持开启，本机自签名服务在握手阶段被拒绝，无 HTTP 请求或文件产物。见 `main20-live-http-final.txt`、`main20-untrusted-tls.txt` 和 `main20-20261028_235106/http-cover-result.json`。
- **真实失败修复：** 首轮 40 张均因 Windows OpenSSL 未加载系统 ROOT 而证书失败；补只读系统根信任后同批成功。实际 WebP 图片又暴露 Drogon 普通 header 推断回退成 HTML 的问题，改为保留自定义 MIME 后通过内容验收。封面迁移在发布前改用 `2026102802`，避免占用并行歌手资料清理已执行的 `2026102801`，DDL 字节未改。
- **跨分支限制：** 指定克隆库已删除 `singers.status` 等字段，本分支旧歌手目录查询仍引用该列，歌手详情联调返回 404；专辑详情返回正确 cover。两会话暂按独立范围保留，待歌手资料变更统一合并后联调目录，不能把该项写成通过。

此前默认库探测的 `SKIP` 已由本轮 main20 真实验收补齐，不再代表当前状态。源库另有会话持续更新，本会话通过只读快照验证，没有干预其任务；运行库 `crawler` 未迁移或写入，云端上传未纳入本次实现。

## 当前推进状态

- 已从 `origin/main` 的 `21b19a5` 建立独立 worktree 和 `codex/artist-album-cover-storage` 分支，当前会话已切换。
- 已读取相关路线、元数据/资源约束、SQL、封面 adapter、HTTP、装配、目录 API 与现有测试源码。
- 本地闭环、SQL、接口和针对性测试已实现，离线/本机与独立数据库回归通过；证据以本次验收及当前交接为准。
- 有效构建位于 `D:/codex-builds/crawler-cover-local-20261028`，规避 C 盘链接空间不足；使用原 preset 和本地链接符号选项。
- 当前下一步是云端自动接入；两会话保持独立，统一合并后联调歌手目录 cover 和发布契约版本。历史专辑 URL 仅在有原响应证据时定向处理。
