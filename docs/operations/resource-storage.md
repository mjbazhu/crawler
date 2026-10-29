# 资源运行与环境操作

用户要求资源运行、手动上传、云端对账或本机排障时读取本文。这里记录原根规范中的本机操作背景；路径、工具可用性和外部状态需在执行前核对。通用配置见[配置说明](../configuration.md)，首次运行见[快速启动](../getting-started.md)，取消、stale、迁移与备份见[服务运行与恢复](service-lifecycle.md)。这些本机记录不代表其他环境可直接使用。

## 本机缓存与手动上传

- 歌曲资源在 filesystem 装配下的现有闭环是：provider 获取 URL、解析、下载本地、回写 `local_path/size_bytes/checksum/status=2`；该装配不自动上传。原本机操作由用户用百度网盘客户端上传 `D:\migu-resource-cache\migu` 到 `/Media/migu`，再以 `D:\migu-resource-cache\cloud-reconcile` 下脚本核对和回写。歌手/专辑封面改走下一节的自动流程，不套用这项手动要求。
- 上述本机百度对账的路径约定为 `cloud_backend='baidupcs'`，`cloud_path='/Media/' + local_path`，不是所有后端的通用约定。该历史对账只检查已写数据库路径与对象存在性，不要求本地文件仍存在；封面自动上传和 content 读取另需完整本地文件及快照，不应因已有云端路径而清理缓存。
- 云端对账脚本目录约定：`audit/` 只读统计和核对，`writeback/` 才允许改库，`lib/` 放公共配置，`runs/` 放运行结果。推荐顺序是 `audit/summarize-missing-cloud-paths.ps1` -> `audit/reconcile-cloud-paths-meta.ps1` -> `writeback/backfill-existing-cloud-paths.ps1` -> `audit/verify-written-cloud-paths.ps1`。遇到 `cloud_unknown.csv` 或 BaiduPCS-Go `EOF`/网络错误时必须低并发重跑核对，不得直接当作云端缺失标记失败。

## 歌手与专辑封面云端接入

两类封面通过现有 `POST /api/migu/covers/process` 自动上传并回写，开启 `CRAWLER_RESOURCE_CLOUD_UPLOAD_ENABLED=true` 即进入云端阶段。`CRAWLER_RESOURCE_CLOUD_UPLOAD_BACKEND=baidupcs` 使用已登录客户端及 `CRAWLER_RESOURCE_CLOUD_ROOT`，`rclone` 使用 RC 鉴权与 Google Drive 命名 remote `CRAWLER_RCLONE_TARGET_FS`。完整配置示例见 [.env.example](../../.env.example)，实现及验收见[云端存储](../roadmap/metadata-cover-cloud-storage.md)。歌曲的既有手动上传流程继续独立维护。

这些开关启动时装配，修改后重启；开启本身不启动扫描。保留 `CRAWLER_RESOURCE_STORAGE_BACKEND=filesystem`，分别以 singer/album 提交 process；不要为了封面上传去开启歌曲的旧 rclone copyurl 存储模式。当前一次选择一个上传后端，切换目标后再补传；资源表只留最后确认的单份云端回执，双端持久化识别仍待[统一回执重构](../roadmap/resource-cloud-receipts.md)。

本地成功后上传；历史已下载文件直接补传。百度回读比对 SHA-256，Google 使用 RC multipart 上传快照并核对远端大小与完整摘要，不要求两个服务共用磁盘。云端状态与本地状态分列保存；失败、超时、EOF 或核验未知不会销毁本地成功，也不会伪装为云端成功。云端总超时 `CRAWLER_RESOURCE_CLOUD_TIMEOUT_MS` 默认 120000 ms；再次 process 会先核对目标对象，成功回执匹配时跳过。

API 的 `cloud.path` 是最后确认的对象路径，应同时检查 `cloud.status/current`，不能当公开图片 URL。远端已存在相同路径但内容不符时报告错误；刷新必须经 `refresh=true` 和明确资源 ID，生成新本地文件后旧云端认领失效。云端 stale 恢复只推进自己的代次，不改变本地下载状态。

## TLS 与代理排障

- 历史 Windows/OpenSSL 测试曾通过关闭 TLS 校验定位证书信任问题；该历史记录不代表当前推荐配置。2026-10-29 封面实测确认 OpenSSL 默认路径未加载系统信任根，现已增加只读 Windows ROOT 装载，证书链及主机名校验保持开启。20 张歌手图、20 张专辑图通过，并以本机自签名证书确认拒绝不受信任的连接；证据见[封面专项](../roadmap/metadata-cover-download.md#本次验收结果)。后续仍出现证书错误时检查系统根和代理证书链。
- proxy 不是当前上线前置条件。先无 proxy 小批量生产跑通，再接 proxy 做 A/B 对比和提并发，避免同时引入过多变量。

## 历史上传实验

原规范记录 BaiduPCS-Go/Web/桌面客户端实际都走 `locateupload -> precreate -> rapidupload/superfile2 -> create` 类似链路，真实速度主要受本机上行或百度侧上传通道限制。原实验日期、代码版本与日志位置未记录，本次未复验；不要据此宣称当前环境的性能。

是否启用自动上传由 [资源链路约束](../migu/resources.md) 管理；需要重新做上传实验时，遵循用户明确授权的范围。真实下载或数据库测试条件见 [验证要求](../agents/validation.md)。
