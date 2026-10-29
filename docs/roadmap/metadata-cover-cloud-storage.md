# 歌手头像与专辑封面云端存储

更新日期：2026-10-29。分支 `codex/artist-album-cover-storage`。百度与 Google 已接入同一封面流程，各自 40 张真实图片验收完成。用户要求开启配置后自动上传并正常回写，既有关闭自动上传的约定不限制这两类封面。

本文件记录当前已实现的单份云端回执基线。后续已确认将实际存储平台与上传工具区分、把三类 resources 的云端结果迁入统一回执表，方案见[统一云端回执重构](resource-cloud-receipts.md)；该方案尚未实施。本次先整理并提交当前实现、创建 PR，按用户最新指示暂不合并。

交付位于[草稿 PR #7](https://github.com/mjbazhu/crawler/pull/7)。当前分支保留 `21b19a5` 功能基线，与 main `69a157f` 的 7 处冲突尚待集成处理；历史真实上传与本轮 15 项回归属于功能分支验证，未计为最新 main 的组合验收。冲突及接续顺序见[当前交接](../agents/current.md#pr-交付与接续入口)。

## 入口和配置

继续调用 `POST /api/migu/covers/process`。元数据采集仍只保存 URL；封面处理完成本地文件确认后，按服务器配置执行云端上传、内容核验和数据库条件回写。已下载文件直接补传，无需手动上传、CSV 对账或 `refresh`。

```dotenv
CRAWLER_RESOURCE_CLOUD_UPLOAD_ENABLED=true
CRAWLER_RESOURCE_CLOUD_UPLOAD_BACKEND=baidupcs
CRAWLER_RESOURCE_CLOUD_TIMEOUT_MS=120000
CRAWLER_STORAGE_PATH=D:/migu-resource-cache
CRAWLER_RESOURCE_CLOUD_ROOT=/Media
CRAWLER_BAIDUPCS_GO_PATH=D:/AI/code/BaiduPCS-Go/BaiduPCS-Go.exe
```

Google 切换后端为 `rclone`，继续使用现有 `CRAWLER_RCLONE_RC_URL/RC_USER/RC_PASSWORD/TARGET_FS`。例如 `CRAWLER_RCLONE_TARGET_FS=gcloud:Media`，要求已配置且已授权的 Google Drive 命名 remote。认证由私有环境或已登录客户端提供，不进入 API、迁移或任务日志。完整示例见 [.env.example](../../.env.example)。同一服务器选择一个后端，切换后端或根目录后，已有本地文件会补传至新目标。

| 后端 | 上传及核验 | 完整回执路径 |
| --- | --- | --- |
| 百度 `baidupcs` | 复用 BaiduPCS-Go uploader；独占输入快照；`meta` 明确判定存在/缺失/未知，上传后回读完整文件比对大小与 SHA-256 | `/Media/` 加本地 `migu/singers/...` 或 `migu/albums/...` |
| Google `rclone` | 复用现有 RC 配置及 Basic Auth；`operations/uploadfile` multipart 传递本地快照；`operations/stat` 比对大小及完整 SHA-256 或 MD5 | `gcloud:Media/` 加同一本地相对路径 |

Google 不依赖 RC 和 crawler 共用磁盘。协议依据为 [rclone 官方 RC 文档](https://rclone.org/rc/)。云端路径不是公开 HTTP 图片地址，本地图片继续通过返回的 `local_url` 读取。歌曲的既有上传装配保持独立。

## 共用实现与职责

- [MetadataCoverService](../../src/application/use_cases/MetadataCoverService.cpp) 的同步与后台入口共用本地确认后的上传步骤；默认候选包含本地待下载/失败，以及本地成功但尚无有效当前云端回执的项。
- [MetadataCoverCloudWorkflow](../../src/application/use_cases/MetadataCoverCloudWorkflow.cpp) 使用窄的 `CloudFileStore` 和 `MetadataCoverCloudQueue` port，统一重试、回执校验、条件确认和恢复，不包含厂商分支。
- [组合根](../../src/infrastructure/app/CloudFileStoreComposition.cpp) 按开关和后端构造 [Baidu adapter](../../src/infrastructure/storage/baidupcs/BaiduCloudFileStore.cpp) 或 [rclone adapter](../../src/infrastructure/storage/rclone/RcloneCloudFileStore.cpp)。开关关闭时不创建 uploader，主入口和 ServerApplication 不再强制关闭已启用的封面云端配置。
- [有界进程执行](../../src/infrastructure/execution/process/ProcessRunner.cpp) 使用 Windows Job 管理本次子进程、总超时和取消，最多捕获 64 KiB 输出；外部原始输出不转发 API。百度复用既有 upload 命令构建，旧歌曲流程不改接封面 ID。
- 两张封面表共用同一模型和状态语义，表级 SQL 仍分别保存，避免同数字资源 ID 跨所有者写入。Google 与旧 rclone copier 共用 RC 鉴权实现。

该设计用同一流程和 port 消除两种封面、两家后端之间的重复编排（DRY），让厂商适配与业务确认各自承担单一职责（SRP）。只扩展既有 process 和恢复入口（KISS），不新增通用网盘管理、手动上传接口或双写编排（YAGNI）。

## 数据与恢复边界

正式迁移 [V2026102901](../../sql/mysql/migrations/V2026102901__metadata_cover_cloud_storage.sql) 为 `singer_resources`、`album_resources` 新增 `cloud_backend/path/size_bytes/checksum/status/uploaded_at`、独立认领代次/时间/批次、脱敏错误及 JSON 上传意图。checksum 为 `ee00f3811554d6eb404813a48f58b607`；首次执行后不再修改迁移字节。

| 云端状态 | 含义及后续 |
| --- | --- |
| 0 | 待上传，或本地成功刷新后需要确认新文件 |
| 1 | 已认领上传/核验；其他 worker 不能重复认领 |
| 2 | 远端内容与本地快照已核实，DB 回执已确认 |
| 3 | 明确失败，再次 process 可重试 |
| 4 | 上传、回读或提交结果未知；重试先核对确切对象 |

本地 `status/local_path/size_bytes/checksum` 保留本地语义。云端失败不回滚本地成功，不删除本地文件，不覆盖最后确认的云端对象回执。JSON `cloud.current` 表示回执是否匹配当前文件及已启用的目标；有历史路径不等于当前文件已上传。

上传不持有数据库事务；认领时保存最小上传意图，成功回写同时比较云端代次、本地成功状态及路径/大小/SHA-256。DB 提交响应丢失时重读确认事实。旧代次或旧文件不能覆盖新结果；失败响应也重读当前数据库快照。

本地成功刷新使旧云端认领失效并置待上传；失败刷新保留原图和最后确认的云端回执。专辑别名归一迁移所选本地文件及其云端回执，不继承旧 worker 的运行态。stale 恢复独立推进云端代次并置待核对，不删除可能仍被引用的对象。

EOF、超时、无效元信息或缺少可靠摘要均不当作不存在或成功。百度按实际客户端 `类型 文件/目录` 与旧 `文件类型` 输出识别对象；Google 远端大小相同但摘要不同仍报错。目标同名内容不符时不盲目覆盖。

## API 契约

[API 顶部清单及说明](../api/mg/migu-api.md#歌手头像与专辑封面本地下载)、[静态 OpenAPI](../api/mg/openapi-migu.yaml) 与运行期 `/api/openapi.json` 统一为 `1.9.0`。新增 `cloud` 对象与 `cloud_status` 筛选，不新增路由。

开启云端时，item 需要本地及云端均确认才成功；云端失败的 item/batch 失败，但本地 `status=2` 和 `available/local_url` 保留。再次 process 只补云端阶段。`refresh` 仍明确表示重新获取图片，须显式资源 ID。恢复入口返回当前启用阶段的恢复总数。

## 本次验证

本轮代码、迁移及实际接入验证如下；此前本地下载结果不单独计作云端成功：

- `demo` 及受影响 target 构建通过，最终构建日志 `cloud-demo-final-build.txt`、`cloud-formatted-build.txt`。
- 15 项离线/本机回归全部通过，见 `cloud-offline-final.txt`，包括共用云端流程、两个 adapter、真实子进程边界、API/路由、旧同步/异步歌曲流程及存储恢复。
- 两个新独立 MySQL 库 `crawler_ownership_test_c2901_cloud_20261029_012342`, `crawler_ownership_test_c2901_local_20261029_012342` 通过云端与本地回归；覆盖新迁移及重跑、两类所有者、并发认领、旧代次、刷新、失败保留本地、候选筛选及专辑别名。汇总 `mysql-cloud-latest.json`。
- 186 份运行期 SQL prepare 通过，见 `cloud-sql-prepare.txt`。两份 OpenAPI `1.9.0` 的 4 个路径、7 个 schema、2 个目录属性与实际服务一致；迁移 V2802/V2901 checksum 均一致，见 `cloud-contract-final.txt`。
- 百度真实 20 张歌手图、20 张专辑图均完成远端回读 SHA-256 比对与 DB 回写；第二次处理均无候选。最初客户端 `meta` 只输出“类型”，旧解析将已上传对象留为待核对；修复后重新核对同一对象并确认，没有用退出码冒充成功。结果 `cloud-live-baidupcs-20261029_0130/result-both-20.json`。
- Google 真实 20 张歌手图、20 张专辑图均上传、核验和回写；第二轮无候选。另用独立 rclone CLI 枚举 40 个对象，无重复对象，每个大小和 MD5 均匹配本地字节及已回写 SHA-256，见 `cloud-live-rclone-20261029_0130/independent-remote-verification.json`。
- Google 原 `.env.test` RC 地址连接重置；本轮用已有本机 Drive 授权配置和官方 rclone `v1.75.2` 便携版启动仅监听回环的临时 RC。发布包经官方 SHA256SUMS 校验，来源为 [官方发布](https://github.com/rclone/rclone/releases/tag/v1.75.2)。实际业务部署仍需保持其配置的 RC 服务可达；没有修改原私有 env 中的 RC 地址。
- 实测使用已验证克隆库 `crawler_ownership_test_cover_main20_20261028_235106`、已有 40 张图片根和独立 `crawler-tests/cover-cloud-20261029_0130` 云端子目录。每个后端的本地路径、大小、SHA-256、状态 2 与内容读取均保持正常。源 main20 库和 main 工作区未写入。
- 真实上传包含同步小批与后台批次；后台记录、明细和事件均已保存。长批次应使用默认 `async=true`，避免 HTTP 空闲连接提前关闭；首次百度同步 20 项的连接虽提前关闭，任务实际已完成，后续按后台入口重读确认和验收。
- 两个自建 crawler 服务及临时本机 RC 已按 PID、exe 和启动时间验证身份后停止。云端测试对象、克隆库和证据保留供复核。
- 六个新增实现的 clangd 编译/preamble/AST 阶段无编译诊断；四个完整 `0 errors`，另两个仅有 ExtractFunction 编辑器 tweak 自检错误，不能称全部 clangd 命令通过。格式、UTF-8 无 BOM、文件长度和文档链接通过，Git diff 无空白错误。

证据根：`D:/codex-runs/crawler-cover-local/20261028/`。本轮未暂存、提交、推送或合并。

用户确认的跨分支边界继续有效：`singer-profile-completion` 与本任务保持独立，统一合并后联调歌手目录；不混入另一会话的字段清理。该目录兼容问题不影响封面队列、文件及云端 SQL 的独立验收。
