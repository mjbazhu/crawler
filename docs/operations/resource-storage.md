# 资源运行与环境操作

用户要求资源运行、手动上传、云端对账或本机排障时读取本文。这里记录原根规范中的本机操作背景；路径、工具可用性和外部状态需在执行前核对。记录用于执行前核对环境，不代表这些本机路径在其他环境可直接使用。

## 本机缓存与手动上传

- 当前稳定闭环是：后端只负责 provider 获取 URL、解析、下载到本地、回写 `local_path/size_bytes/checksum/status=2`；用户用百度网盘客户端手动上传 `D:\migu-resource-cache\migu` 到 `/Media/migu`；随后用 `D:\migu-resource-cache\cloud-reconcile` 下脚本做云端核对和数据库回写。
- 云端路径约定固定为 `cloud_backend='baidupcs'`，`cloud_path='/Media/' + local_path`。核对已写路径时只检查数据库路径字段和云端对象存在性，不要求本地文件仍存在；本地缓存可被用户手动移动或删除。
- 云端对账脚本目录约定：`audit/` 只读统计和核对，`writeback/` 才允许改库，`lib/` 放公共配置，`runs/` 放运行结果。推荐顺序是 `audit/summarize-missing-cloud-paths.ps1` -> `audit/reconcile-cloud-paths-meta.ps1` -> `writeback/backfill-existing-cloud-paths.ps1` -> `audit/verify-written-cloud-paths.ps1`。遇到 `cloud_unknown.csv` 或 BaiduPCS-Go `EOF`/网络错误时必须低并发重跑核对，不得直接当作云端缺失标记失败。

## TLS 与代理排障

- 当前 Windows/OpenSSL 环境下，Migu 下载域可能因为证书链导致 `TLS handshake failed: certificate verify failed`。真实测试时如出现大量 `network_failed` 且 message 指向证书校验，优先检查 `CRAWLER_OUTBOUND_VERIFY_TLS` 或 CA 配置；本地实测使用 `CRAWLER_OUTBOUND_VERIFY_TLS=false` 后下载正常。
- proxy 不是当前上线前置条件。先无 proxy 小批量生产跑通，再接 proxy 做 A/B 对比和提并发，避免同时引入过多变量。

## 历史上传实验

原规范记录 BaiduPCS-Go/Web/桌面客户端实际都走 `locateupload -> precreate -> rapidupload/superfile2 -> create` 类似链路，真实速度主要受本机上行或百度侧上传通道限制。原实验日期、代码版本与日志位置未记录，本次未复验；不要据此宣称当前环境的性能。

是否启用自动上传由 [资源链路约束](../migu/resources.md) 管理；需要重新做上传实验时，遵循用户明确授权的范围。真实下载或数据库测试条件见 [验证要求](../agents/validation.md)。
