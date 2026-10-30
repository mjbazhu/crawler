# 持久资源任务与三路传输

更新日期：2026-10-30。对应[统一资源方案](../roadmap/resource-cloud-receipts.md)的持久任务、三路运行和基线阶段。本文区分已接线能力与剩余限制，实际验证见[当前交接](../agents/current.md)，不能据此省略生产验收。

## 当前接线范围

- `ResourceTransferJobRepository` / `MySqlResourceTransferJobs` 已实现父操作、逐端任务和独占槽。一次登记最多 200 个资源、600 个目标项；local、baidu、gdrive 都有独立认领、取消、失败、重试和恢复。
- `ResourceTransferWorker` 使用独立来源和目标 port，来源、传输分别使用有界配额，等待目标轮转。每次认领冻结超时、有限重试、大小、限速及版本；排队不消耗单次传输预算。
- `ResourceSnapshotSource` 复用既有歌曲合法 listen/token/版权与流式下载链、封面下载与完整图片校验；派生产物读取已经登记的受控文件。各路私有目录、空间预留及读租约独立，构造不创建目录。
- 工厂提供 `local_transfer` 和 `destination(local/baidu/gdrive)`，本地使用实际分片保存、完整提交和 SHA-256 复核，两端使用文件快照策略；正式 local 根延迟到真实 local 写入。
- Server 默认 legacy；显式 parallel 装配独立三路、来源/传输保留配额、逐任务截止/取消和 v2 接口。配置固定 legacy view 后桥接旧歌曲/封面 process、持久进度/停止及封面字段；已知元数据/补建/治理入口接入持久准入并保留原语义，未知写入口或未配置旧process投影仍明确503，见[接口边界](../api/mg/migu-api.md#parallel-下的旧客户端兼容)。旧 `CloudFileStore` 的 8 MiB bytes 接口保留，统一文件策略不经过该上限。早期云端验证使用替身；后续真实工厂三路及独立取消的分层证据见[验收记录](resource-release-readiness.md)，不等同于实际库业务服务已启动。

窄 port 位于[任务仓储](../../include/crawler/application/ports/persistence/ResourceTransferJobRepository.h)、[来源](../../include/crawler/application/ports/provider/ResourceTransferSource.h)与[目标传输](../../include/crawler/application/ports/storage/ResourceTransferDestination.h)。新增 HTTP 契约见[v2 API](../api/mg/migu-api.md)，没有无版本地替换旧成功结构。元数据事务按业务身份取真实来源主键，失效 worker 不覆写另一代次的资源错误状态。

列表附加通过只读 POST resources/status 一次查询最多200个完整文件身份；四类资源和本地/双端事实来自同一SQL快照，按请求顺序保留缺失占位。全部目标current、历史skip、原生状态和核验强度分开；只读在legacy/维护中可用，既有目录响应不变，调用方以一批请求附加。接口与示例见[v2说明](../api/mg/migu-api.md#列表批量附加本地与双端状态)。

活动基线可用 baseline-maintenance 明确退回维护，全事务关闭三端准入、保留全部回执/策略/意图并登记治理审计；后续复用审阅/封存/激活。该命令要求操作方已停止writer，不代表外部进程已停止；审计失败整笔回滚，重复执行不重复记账。

## 表与迁移

`V2026102911` 增加只读历史发现的 run/observation 表和数据库统一队列限额；[历史发现说明](resource-history-discovery.md)包含新平台映射、续跑、证据分级及报告审阅边界。

`V2026102907` 增加完整基线记录与按内容代次的历史排除表。四类范围先导入/明确排除，再 review、seal、activate；封存不开放调度。步骤及目标切换边界见[维护工具](resource-cloud-import.md)。

| 对象 | 职责 |
| --- | --- |
| `resource_transfer_operations` | 真实父操作、幂等键、规范请求摘要和首次受理快照 |
| `resource_transfer_tasks` | 四类资源的逐端冻结版本、预算、认领及本次对象意图；不随资源删除而丢失历史 |
| `resource_transfer_slots` | 同一 owner/id/destination 的独占占用；跨操作 busy 不接管取消权 |
| `resource_discovery_runs` / `resource_discovery_observations` | 固定范围、页租约、检查点及只读逐对象证据，不直接改变 skip/回执 |
| 资源表与 `resource_cloud_receipts` | 分别保存本地、各云端的当前任务和最后确认事实；新任务不擦除已有成功 |

`V2026102908` 增加 resource_producer_tasks；`V2026102909` 增加 resource_legacy_profiles/batches/operations；`V2026102910` 增加 resource_transfer_related 和父操作/任务生成列及索引。音频确认与其合法关联资源子操作受理同事务；只持久发现类型掩码，不保存临时 URL。

`V2026102903` 增加三张任务表及 artifact 原生认领字段。`V2026102904` 捕获来源变化；`V2026102905` 将 URL/字符串身份比较明确为字节比较，避免默认不区分大小写的排序规则隐藏变化。`V2026102906` 保留旧版本已确认本地文件，只递增在途认领的代次，增加音质不会改变已经完成的关联资源认领。已执行迁移不改内容；schema 状态同时检查版本、表、列、约束及八个来源触发器。

歌曲来源/音质参数、封面 URL 变化推进 source_revision 并使在途原生认领失效，不因此自动重下已完成文件，也不撤销同内容代次的历史 skip。资源归属/类型/音质身份变化及 artifact 来源、processor/schema 变化推进 content_generation，清空新代次参考摘要，旧 skip 不被错误复用。明确 refresh_content 在短事务中推进一次内容代次；有其他占用时拒绝刷新。

来源捕获与元数据变更同事务，不依赖所有调用方主动记得更新版本。它不能代替部署前停旧 writer、基线封存、producer 拆分或别名合并的业务迁移。

## 登记与幂等

首次受理原子登记全部选定目标，冻结内容代次、来源修订/身份、目标修订/指纹、工具及预算。幂等重放先按原始规范请求核对，返回第一次的资源和配置；相同键不同请求冲突，配置变化不重新展开缺省目标。

认领按实际可执行时间和任务 ID 排序；pending 与 retry 各用目标/状态索引取首项，再合并最多两行。新任务登记真实可执行时间，不用零值持续挤占已到期重试；重试退避未到期不能提前认领，最终更新在事务内再次检查到期条件。

逐端优先判断有效历史 skip，再判断维护、已有确认、busy、输入和配置。skip 不读文件、算 hash、刷新 token 或探测网盘，不因某端离线改成传输。跨操作 busy 保留原任务 ID，不把它算成已保存。普通 process 不解除已经取消的策略，明确 retry 与 repair 各有边界。

正式旧歌曲 SQL 认领同时排除新的 local 占用槽，避免新任务 pending 时被旧队列抢领。相关 SQL 与 C++ 绑定一起部署，不能混用旧二进制和新 SQL。

## 对象意图、参考摘要与确认

1. 每路独立取得完整快照；三路尚无可复用快照时可以分别访问源站，受全局来源配额约束。临时文件不登记正式 local 成功。
2. 目标 prepare 只写私有 staging，发布前取得实际独占对象路径。beginAttempt 在同一短事务检查当前认领、来源、目标及取消，绑定参考 SHA-256/大小，登记精确意图。
3. 第一份完整快照绑定参考，不等待本地或任一云端成功。其他路发现内容不同记 content_conflict，不能自动重试直到碰巧一致。
4. 正式发布后 complete 再比较任务/原生认领、内容、来源、目标指纹/修订和取消，只有匹配才确认回执。数据库确认未知时保留对象及补偿信息。
5. 本地 checksum/size 仅描述本地文件；云端先完成不改写它。MD5 远端匹配记 hash_matched，不能制造远端 SHA-256 核验。
6. 回执区分本次实际上传和已有对象核对。后者只更新实际核验时间；没有可靠旧上传时间时 uploaded_at 保持未知，不以修复时间冒充上传时间。

正常 process 保留已满足当前版本的确认事实；这不等于本次在线探测过对象。后续查询接口仍需把最后确认、是否当前、实际可用性与核验强度分开。

## 取消、未决与恢复

- pending/retry 没有对象意图时取消并释放槽；继承旧对象意图的修复任务转 uncertain 并保留槽。running 写本任务 cancel_requested，由本端协作结束。父操作取消传递给同目标的 producer 及 audio→关联文件→歌词产物两层子操作，不接管无关操作；父关系有独立索引，登记和取消统一按祖先到后代取锁。
- 发布前已明确停止，可记 cancelled。已经发布却因取消无法确认，记 uncertain 并保留槽、路径、摘要和大小；普通 process/retry 返回 reconcile_required，禁止直接重新下载/上传。
- repair 先核对旧意图，匹配时直接确认，明确不存在才取得来源。未知、认证/网络失败和冲突均不能当作缺失。“明确不存在”还要求旧写入已经结束或可靠取消，不能把在途上传暂时查不到对象当成可重传。目标或内容版本已改变时，显式 repair 可退休旧占用，保留旧历史，再按当前版本规划；不会把旧对象认作新目标成功。
- recover 只针对显式目标、按有界页恢复过期认领并推进 claim_generation。同步目标在预算到期后才返回成功，也转为待核对，不晚报成功。没有副作用意图的任务可在预算内重试；有意图的任务必须待核对，旧 worker 无法回写新结果。
- 父操作只在所有子项终结后进入终态；混合成功/取消或任何失败、未决、busy 为 partial。全 skip 可以 completed，但明细保持 skipped，不能伪造实测成功。

文件 journal 按四类 owner 核对。当前路径仍被任一资源引用时保留；原生状态改变或旧主键被合并不能单独作为删除依据。持久 pending/running/uncertain 任务仍拥有对象时保持 Active；数据库不可用返回未知。artifact 不再按同数字的歌曲主键核对。

历史导入同样拒绝已有持久槽、running 或 uncertain 回执，不用人工声明掩盖在途或未决副作用。

## 派生文件与兼容校验

派生文件先读取受控既有文件；配置了合法 producer 时，缺文件可从冻结的原 MRC 来源私有快照重新生成，核对 source_checksum、处理器/结构版本及产物参考。缺少生成资格时为 waiting_input。旧 SHA-256、MD5、SHA-1 和大小仅按实际算法比较；历史 skip 在这些动作前结束。

MRC 受理时在同事务登记独立 producer，使用独立认领、超时、有限重试和 worker；下载来源不再同步解码，不消耗父 MRC 传输预算。每个目标内 producer 与普通传输共享来源快照及全局来源配额。缺合法密钥为持久 waiting_input，且来源请求为零。生成完成在一个事务内核对认领/父取消/来源/目标/处理版本，绑定 MRC 参考、登记两产物、受理当前目标子操作并确认 producer，任何一步失败整笔回滚。查询和明确补排见 v2 producers 接口，运行期每30秒按660秒安全阈值恢复过期producer认领。

普通 process 的历史 skip 仍在来源读取前结束；需要补历史派生内容时显式 action=produce_artifacts，仅接受 song/MRC。父 MRC 项记 skipped/producer_only，不建立原文件传输槽，也不清除三端 skip/回执；真实永久来源限制仍持久阻断。v2 操作聚合等待 producer 及其子操作；旧歌曲完成只等待原合同必要范围，缺 key 不拖住旧批次。纯内容缓存至多16份/64MiB，不是持久快照，淘汰/重启可能重新读取 MRC，不能保证整个批次只读取一次。

## 云端文件策略

[RcloneResourceTransfer](../../include/crawler/infrastructure/storage/rclone/RcloneResourceTransfer.h) 与 [BaiduResourceTransfer](../../include/crawler/infrastructure/storage/baidupcs/BaiduResourceTransfer.h) 实现同一目标 port。共用文件准备/会话 helper 验证实际快照 SHA-256、大小与租约，工厂只装配，不领取任务或探测目标。新增策略的装配错误保留在 `transfer_result_code`，不会破坏仍可用的旧封面 `store` 或另一平台。

新对象布局为 `prefix/resources/v1/{owner}/{resource_id}/g{content_generation}/t{task_id}-c{claim_generation}-{random}/{snapshot_filename}`。随机独占目录保留原生工具的文件名，并隔离版本、认领与不同尝试；发布前将完整路径登记到数据库。历史对象不因布局变化而重命名。当前布局不能代替真实账号/drive/root 身份核验，正式配置和目标指纹还须包含布局版本。

### 配置复用与工具冻结

`provider` 表示实际网盘，`adapter` 表示传输工具，二者独立：local/filesystem、baidu/baidupcs、baidu/rclone、gdrive/rclone 是当前允许的组合。百度不要求必须使用 BaiduPCS-Go，rclone 也不自动代表 Google。

- `ResourceCloudTarget.adapter` 留空时继承现有 `resource_cloud_upload_backend`；`rclone` 配置留空时原样复用既有 RC 地址、用户名/密码、remote/root 和预算。不读取、复制或改写 rclone 登录文件，不要求重建 remote。
- 双端可分别选择工具，并按端覆盖 RC 配置和客户端；默认按端创建客户端，避免改写共享配置。两个 rclone 目标若使用完全相同的 RC 地址、认证和 TARGET_FS，分别标成百度/Google 会返回 `cloud_target_ambiguous`；local 与其他正常目标不受单端坏配置影响。不同别名或连接仍须后续真实身份核对，不能仅靠配置差异证明是两个平台。
- 旧 Server 使用 `CRAWLER_RESOURCE_CLOUD_PROVIDER=baidu/gdrive` 声明 rclone 的实际平台，复用全部原有 `CRAWLER_RCLONE_*`。baidupcs 可省略平台声明。当前 legacy rclone 缺失或无效声明会在启动装配拒绝，早于 copyurl/上传；通用适配器保留，但不能据工具名猜 Google。配置入口见[配置说明](../configuration.md#存储模式与封面云端)。
- 首次登记将 adapter 冻结在已有父操作 `request_snapshot.adapters`，任务查询一并读回，不增加迁移或保存认证。相同幂等请求重放不按新配置换工具；认领要求 worker 与冻结工具一致，否则记录 `adapter_changed`。旧任务缺少工具时保持 `adapter_unresolved`，没有意图则阻止执行，有意图则保留 uncertain 和独占槽，不按平台补猜工具。
- prepare、inspect、版本检查和回执确认继续校验冻结工具；百度/rclone 的远端 MD5 可记 hash_matched，不能接受其他工具冒充本次结果。旧未决对象的 repair 必须使用原工具；切换工具不能接管或退休原占用，先用原工具完成核对。有效历史 skip 仍先于配置和来源动作，缺工具或离线不会导致历史重复下载。

正式接线使用[parallel 配置](../configuration.md#parallel-三路运行)，仅修改旧单端开关不会启用三路。`target-probe` 和启动身份探测使用相同核验实现；目标恢复在线后当前需要重启重新装配。

### rclone（已声明的百度或 Google 目标）

- 新 `HttpClient::streamPostWithOptions` 按需供给正文，Asio 使用 64 KiB 缓冲、每连接 strand、独立总超时和 25 ms 取消检查。multipart 头尾有界，正文读取期间再次校验 SHA-256，完整通过后才发送结束符；复用既有 TLS/代理连接能力。旧客户端默认返回 `stream_upload_unsupported`，不会回退为全量聚合音频。
- 元信息、上传及核对 RPC 都有单次上下文和有界响应。`operations/stat` 必须返回合法文件类型、精确大小及可用摘要；新快照可与远端 MD5 比对，但只记 hash_matched，远端完整 SHA-256 匹配才记 sha256_verified。
- repair 没有原快照的 MD5 时，不重新获取歌曲或读取正式 local。远端 SHA-256 不可用则对精确对象调用 `operations/hashsumfile(download=true)` 回读；同时校验返回的 hashType 和摘要格式，其他算法不能冒充 SHA-256。
- 本机发送按任务限速，RC 通过本次 `_config.BwLimitFile` 约束文件上传/回读，不调用全局 `core/bwlimit` 或 `options/set`。每次 RC 调用固定 Retries/LowLevelRetries 为 1，额外重试只由持久任务预算决定。真实 daemon 的停止、缓存、版本兼容与后端限速效果仍需在隔离环境验收。

协议依据：[rclone RC 的 stat/uploadfile/hashsumfile 与单次配置](https://rclone.org/rc/)、[rclone 每文件限速参数](https://rclone.org/flags/)。当前核对日期为 2026-10-29；不能以在线文档替代部署版本的实际能力验证。

### 百度 / BaiduPCS-Go

- 原生 upload 直接消费本任务持有租约的文件，不复制整个音频到内存或正式 local。参数用原生命令引用，固定 `--retry 0`、`-l 1` 和独占路径的 skip 策略；每次进程由现有有界 runner 管理，只取消本次进程及其子进程。
- meta 只识别明确的缺失错误码和精确路径/类型/完整整数大小；普通诊断中的数字不代表缺失。解析范围是已核对的表格格式，其他版本格式保持 Unknown。分片上传的 MD5 不作为强证据，上传后或只读修复都实际回读并以 64 KiB 缓冲计算 SHA-256。
- 回读使用本端独立 staging 根和空间预留，单项预留完整大小加 64 KiB 客户端状态开销，默认全部回读合计 8 GiB。已知大小超过配置时在上传前拒绝；活跃回读检查目录累计大小和链接，超限取消自己的进程。它是准入及轮询约束，不是操作系统硬磁盘配额；不能把 64 KiB 本进程缓冲宣传成对外部客户端内存/瞬时写盘的硬保证。
- 所核对客户端将 max_upload_rate/max_download_rate 作为共享 config 设置，没有采用可验证的逐调用覆盖。`bytes_per_second>0` 在持久任务登记时判为不可用，直接策略调用返回 `baidu_task_rate_unsupported`，不忽略设置，也不修改共享配置。这只限制 baidupcs 工具；百度选择 rclone 时复用上述逐调用限速。正式配置接线仍须保留来源获取前的准入。

依据：[原生上传及配置参数](https://github.com/qjfoidnh/BaiduPCS-Go/blob/main/main.go)、[meta 输出与分片 MD5 边界](https://github.com/qjfoidnh/BaiduPCS-Go/blob/main/baidupcs/file_directory.go)。适配器不读取、复制或输出用户登录配置。

### 未决对象与停止证明

两端 inspect 只读取已经登记的受控对象，不上传、不删除，也不获取来源。匹配后只增加实际核验事实，uploaded_at 没有可靠旧值时保持未知。未决上传缺少远端停止证明时，即使查询当前不存在也返回 Unknown；不能将“暂时看不到”当作新上传许可。只有全新独占尝试在上传前明确缺失，才执行第一次上传。HTTP 连接关闭或原生进程停止不被冒充为远端提交已停止的证明。

当前因此可以只读确认已有对象，但不能自动重传一个仍无法确认停止的旧云端意图。实际账号/drive 身份、远端任务终止证明及相关运维恢复入口仍待补齐；明确目标/内容切换继续使用已有版本围栏和历史意图退休规则。

## 验证与剩余接线

2026-10-30 统一查询/维护切换阶段：受影响目标构建成功，6项API/准入、批量四类状态与基线维护两项隔离库业务测试、354份SQL prepare，以及parallel/legacy两次实际维护库HTTP验证通过。11项clangd退出0；本阶段10个v2路径/21个schema精确匹配。查询与维护均复用原表，没有新增migration。证据为同一仓库外目录的 status-verification-final.json、status-offline-final.log、status-static-check.json；具体入口见[当前交接](../agents/current.md)。

2026-10-30 本轮验收：demo/维护CLI与受影响target构建成功；29项离线/本机、10项独立MySQL检查和三次实际demo HTTP演练通过。数据库覆盖旧批次兼容、关联发现/修复/两层取消、独立producer及历史producer-only、原三路、生命周期、新/旧库升级、350份SQL prepare、旧封面云端与完整元数据ownership。HTTP分别关闭/启用兼容，另读取128字节自定义旧编号映射，维护错误封装一致；没有实际内容或上传请求。

脱敏证据仍在 `D:/codex-runs/crawler-cloud-receipts-20261029`：`build-durable-final-contract.log`、`durable-offline-final.log`、`durable-mysql-passed-final.json`、`durable-http-passed-final.json`、`durable-root-clangd-final.jsonl` 和 `durable-static-check.json`。19项定向clangd检查退出0；静态核对包含冻结迁移、SchemaStatus与1.11.0的9个v2路径/17个schema。旧取消重提、后处理截止及API错误封装的回归已修复；早期夹具/prepare失败记录保留。所有MySQL库均显式以 crawler_ownership_test_ 命名，没有动生产crawler或私有配置，也没有真实源站/网盘和生产排空验证。

下述为前阶段历史证据，不能代替本轮结果。

`MySqlResourceTransferJobsTest` 必须使用显式 `crawler_ownership_test_transfer_jobs_*` 独立空库；它覆盖登记/回滚、四类 skip、幂等、占用、版本、重试退避与排队公平性、取消、未决修复、恢复和删除，并调用本机三路执行场景：9 MiB 分片内容、本地实际落盘、具体云端策略与 HTTP/进程替身。百度屏障不阻止其他两路完成，取消后明确 repair 只读核对已经完成的百度对象，不增加三次独立来源请求或上传次数；另验证发布/核对晚到的保护。

`ResourceSnapshotSourceTest` 不连接 MySQL 或真实平台，验证歌手/专辑图片与派生文件来源、独立租约、配额、单次大小、兼容摘要、缺输入及等待配额的取消。歌曲来源由上述独立库场景验证。

前一阶段（同日）的实际验证：demo、维护 CLI 及受影响 targets 构建成功；12 项离线/本机测试、九项独立 MySQL 检查通过。后者包含完整元数据 ownership、旧封面与歌手资料、全新/升级 schema、历史导入、持久任务及 281 份 SQL prepare。其持久任务用例在 `crawler_ownership_test_transfer_jobs_20261029_174156_fe77ae` 通过，同时覆盖未到期退避、新任务不挤占重试、发布和核对晚到的保护。

脱敏结果与命令脚本位于 `D:/codex-runs/crawler-cloud-receipts-20261029`：`execution-regressions-final.jsonl`、`execution-mysql-jobs-budget-final.json`、`execution-mysql-sql-queue-final.json`、`execution-offline-final.log`、`build-execution-budget-final.log` 和 `execution-static-check.json`。这些是隔离库及替身证据，未连接生产 `crawler`，未请求真实源站或网盘；后续版本不能沿用为本次测试结果。

本次云端文件阶段新增 `HttpStreamUploadTest` 和 `CloudResourceTransferTest`。前者以真实回环 TCP 验证 16 MiB 按需供给、单连接主动取消/超时、单 I/O 线程的独立进展及发送限速；后者使用 9 MiB 文件、中文文件名和具体双端策略，核对复用、摘要等级、来源变更、响应丢失、路径/大小冲突、回读超限、空间不足、单端错误及并发取消。完整构建和 19 项离线/本机检查见 `build-cloud-verified.log`、`cloud-offline-final.log`；没有真实云端请求。

新的三路 MySQL 场景在 `crawler_ownership_test_transfer_jobs_20261029_200602_7ac58b` 通过，见 `cloud-mysql-jobs-final.json`；旧封面云端持久化在 `crawler_ownership_test_cover_cloud_20261029_200848_d6cf7b` 通过，见 `cloud-mysql-legacy-final.json`。重型执行夹具拆为独立翻译单元，未通过修改全局编译参数规避 MSVC 对象节数限制。上述日志仍在同一仓库外证据目录。

同日平台/工具解耦的实际验证（本次代码）：

- `build-adapter-selection-freeze.log`：重新 configure，demo、维护 CLI 和受影响 targets 构建成功。`adapter-selection-offline-final.log` 的 19 项离线/本机检查全部通过，包含百度复用既有 RC 地址/认证/remote、双 rclone 独立配置/取消，以及同一目标重复声明拒绝。
- `adapter-selection-mysql-jobs-final.json`：`crawler_ownership_test_transfer_jobs_20261029_210922_451724` 通过。四类历史 skip、具体策略三路场景继续通过；新增百度/rclone 回执、幂等工具冻结、排队后切换拒绝、旧任务缺工具拒绝、native 不支持的限速提前拒绝和跨工具 repair 保留原未决占用。
- `adapter-selection-mysql-legacy-final.json`：`crawler_ownership_test_cover_cloud_20261029_211011_13354f` 旧封面云端持久化通过。`adapter-selection-mysql-sql-final.json`：`crawler_ownership_test_receipt_sql_20261029_211011_f861f8` 的 281 份 SQL prepare 通过；没有把它写成完整 metadata ownership 回归。
- `adapter-selection-clangd-*.log`：16 个受影响生产实现的 clangd `--check --tweaks=DumpAST` 退出 0。编码、格式、本地链接、旧及冻结 migration、SchemaStatus 的 12 属性和 22 个维护写操作契约见 `adapter-selection-static-check.json`。

本次只调整既有任务快照和运行期查询，不新增或回改 migration，不修改私有 env；没有连接生产 `crawler`、真实音乐源或网盘。上述结果验证配置复用及替身执行，不证明部署中的 remote 对应声明的平台或具备实际所需能力。

上述阶段记录之后的克隆/恢复、完整服务强杀和真实云端执行范围，以[当前交接](../agents/current.md)及[上线说明](resource-release-readiness.md)为准。原生 Google Drive 现支持交互 OAuth remote，使用官方账号 permissionId 与实际目录 ID 前后核验，保持已有 RC/remote 配置复用；其他未支持后端仍失败关闭。WebDAV 仍只核验挂载身份，后端换盘而挂载身份不变时须显式推进目标修订；可读不代表有上传权限。当前已知旧治理入口已完成兼容接线，受控规模门槛已通过；实际运行与兼容列收缩分别记录，数据库完善无需先启动业务服务。

## 持久队列容量

容量统一保存在 resource_destination_states，所有服务实例、维护工具及旧接口桥接共用，不使用每实例 MAX_PENDING 环境开关。local/baidu/gdrive 分别配置 max_queued_transfers、max_queued_producers，默认均 10000，允许 1..1000000；queue_limits_version 从 1 开始。限额与执行并发/来源配额不同，调整不改变已受理任务的预算或状态。

```powershell
& './build/msvc-debug/Debug/crawler_cloud_receipts.exe' queue-status --database '<明确库名>'
& './build/msvc-debug/Debug/crawler_cloud_receipts.exe' queue-limits --database '<明确库名>' --destination baidu --expected-version 1 --max-transfers 20000 --max-producers 10000
```

status 返回三端限额、版本及占用；为避免扫描无界积压，最多计到限额加一，exact=false 时该值是下界。文件任务 pending/retry/running/waiting_input/uncertain 均占容量，producer 的 pending/retry/running/waiting_input 同样计入；终态历史、skip、busy 不占容量。uncertain 必须核对后处理，取消请求本身不能释放仍未决的对象。升级时已有队列超过默认限额不会删除旧任务，继续排空或显式提高限额，期间拒绝新增同类活动工作。

受理在同一短事务内锁定目标行、登记任务及producer、用当前锁读检查占用。多实例竞争不会各自按旧快照越限；一个请求任一端的新增活动工作超限，整笔请求回滚（包括槽位、回执、旧批次投影和关联/派生子操作），返回 HTTP 503/error_code=resource_queue_full，Retry-After: 2 且允许浏览器读取该头。2秒是重试提示，不承诺届时一定有容量。多端准入原子，受理后的三端仍独立执行。失败请求不留下半笔批次，也不改变原历史skip；同幂等键的已受理重放、全部skip/busy等不新增活动工作的请求不被容量拒绝。批次中只要还新增活动工作，就按对应端和对应任务类别检查。

显式producer失败重试也受容量约束，超限整笔回滚；自动重试/认领恢复沿用已有活动占用。派生子操作满队时producer按原冻结重试预算有限重试，音频关联入队失败则沿原事务/未决策略保留待核对，不偷偷漏掉子任务或无界重试。配置修改锁同一目标集合，核对 expected-version；拒绝调低到现有占用以下，成功修改与治理审计同事务。相同当前版本和相同值重放不增加版本或审计；旧版本须重读后再提交。CLI不会改写私有配置，也不需要停writer才能原子调整容量。

本保护限制活动工作数量，不限制历史表总行数或磁盘空间。旧进程/脚本若绕过统一仓储直接写表不受此逻辑保护，部署仍须停写、升级并统一writer；不能把数据库配置当成允许新旧二进制混写的保证。
