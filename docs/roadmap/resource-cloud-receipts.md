# 统一资源云端回执与三路并行方案

更新日期：2026-10-30。源码基线：main `83e72e9`，已包含 PR #7。实施分支：`codex/resource-cloud-receipts`。已实现持久三路、独立来源、逐端文件传输、显式 parallel 服务接线、只读目标身份核验、基线封存/激活和 v2 接口。已补旧歌曲/封面 process 的持久兼容投影与独立歌词 producer、统一事实查询、历史发现/采信、队列容量、旧治理兼容与清理预演。当前新增 V2913 物理云列收缩及统一回执读写，保留旧 HTTP 协议与三张 legacy 映射表；实际库执行状态单独记录。全部旧协议退役和未来实际运行仍各有执行条件；下文超出“当前实施范围”的内容保留为设计要求。

用户已授权开始实施，当前已修改代码、SQL、契约和说明，早期数据库验证使用独立 `crawler_ownership_test_*` 库。2026-10-30 按用户授权，在业务服务未运行的实际开发 `crawler` 库执行 V2601→V2912 升级；先备份并恢复隔离副本，后验证实际库19张业务表4,439,979行的保留字段、来源性别映射、389份 SQL prepare 与重复迁移。详见[数据库迁移](../operations/resource-database-migration.md)。结构迁移阶段未启动服务或执行历史采信；同日后续已完成45,558条旧完成资源的双端回执及本地skip补建，91,116条回执和全范围审计/重放核验通过，见[实际导入记录](../operations/resource-cloud-import.md#2026-10-30-实际开发库历史补建)。服务及云端传输仍未启动，私有配置未修改。其他交接材料中的“PR #7 暂不合并”是旧阶段指示，当前代码状态以本基线为准。历史验收仍见[封面云端实现](metadata-cover-cloud-storage.md)和[歌手资料验收](../migu/singer-profile-validation.md)，不能用它们代替本方案的验证。

### 2026-10-30 目标纠偏：四类回执分别关联

用户已澄清最终目标为song、singer、album、song_artifact各一张独立云回执表，每表直接关联相应资源，以cloud_provider区分baidu/gdrive。当前单表方案及下文历史治理/兼容体系作为V2913阶段检查点保留，尚未完成四表收缩，不继续按原完整范围扩展。

接续在检查点之后的新分支实施四表拆分，并将历史治理优先移出常驻运行；任务、互斥、并发与取消恢复按实际必要性保留或复用，兼容链路按明确调用需求收口。现有回执、本地路径、跳过事实和旧校验值须完整迁移；历史审计先归档，已执行migration不改，用新前向迁移在克隆验证后处理实际库。

原方案的启动配置、基线封存激活及当前版本真实写入暂不执行；四表版本稳定后重新确定必要启用方式，再完成小批写入。以下各阶段记录保留作证据，旧方案设计要求不自动变成新目标。

### 当前实施范围

| 能力 | 当前状态 |
| --- | --- |
| 扩展与物理收缩 schema | `V2026102902..12` 增加回执、任务、来源/内容/目标、producer、兼容映射、发现/容量和导入/清理证据；新增 `V2026102913` 封存后删除四资源表31个旧物理云列及2索引，不新增表、不改原44份冻结迁移 |
| 迁移执行及身份约束 | 已支持 migration 专用 `DELIMITER` 分句、四条级联外键和两条 BEFORE 身份触发器，schema 状态检查包含约束缺失 |
| 历史自动发现 | V2026102911 固定四类资源高水位、目标身份、路径映射及页租约；CLI 有界并发只读 stat/meta、暂停/取消/恢复及独立审阅报告；不猜路径、不回读媒体、不自动改变 skip |
| 持久队列容量 | V2026102911 为三端各保存文件/producer限额及版本；统一仓储当前锁读、超限整笔回滚，兼容批次/子操作/显式生成重试共用；503/resource_queue_full及Retry-After；不是历史表大小或磁盘配额 |
| 单端采信与补充基线 | history-preview/apply/status/control已实现；固定原观察/审阅SHA，仅改变本端skip或明确missing后的normal，不入队；完整范围可显式作为基线supplement，封存/激活重新核对。双端声明有import-control及代次围栏，旧取消态仍终止 |
| 清理与兼容 | 已知旧治理接口保留原语义，SQL保护skip/排除/槽/未决意图；三类父实体删除、音质占位清理与专辑别名合并自动在所属事务捕获回执及旧字段证据，四类资源和artifact级联有占用围栏；cleanup CLI保留，均不执行远端删除 |
| 物理收缩与未来退役 | 新读写使用统一回执，历史云快照读取不可变档案；旧 HTTP JSON 和3张 legacy 映射表保留。物理 SQL 依赖与广义旧协议依赖分开盘点，未来整套协议退役门禁不阻碍本次物理列清理 |
| 历史声明导入 | 已实现只读清单预览、SHA-256 固定清单、逐页事务、双端及本地 skip、冲突审计、重跑及游标恢复；使用独立维护 CLI，见[操作说明](../operations/resource-cloud-import.md) |
| 策略判定与旧校验值 | 已接入歌曲同步/异步下载和封面本地/旧单端云用例；历史 skip 先于 listen、文件读取和上传判断，普通/关联歌曲认领 SQL 保留 skip |
| 统一目标装配工厂 | 已提供三端策略及独立来源；显式 parallel 组合根装配后台 worker，工厂构造不发请求，关闭 local 不创建正式根 |
| 持久三路与独立来源 | 已实现一次原子登记、幂等冻结、逐端认领/取消/有限重试/恢复、内容参考与版本确认、独立私有快照、全局配额；本机三路场景实际写 local、两端使用替身，见[执行边界与验证](../operations/resource-transfer-tasks.md) |
| 流式云端 | 已实现 rclone 按需 multipart、单连接预算/取消和单次限速，以及百度租约文件上传、有界私有回读与 SHA-256 核对；旧图片 bytes 接口保留。未决缺失无停止证明时保持 Unknown；百度逐任务限速组合明确拒绝，见[实际边界](../operations/resource-transfer-tasks.md#云端文件策略) |
| artifacts producer | 已实现独立持久计划/预算/worker、缺钥匙等待、失败补排、原子绑定参考/登记产物/受理子操作，以及两层定向取消；显式 produce_artifacts 可补历史 MRC 派生内容且保留原 skip。缓存淘汰后仍可能重取 |
| 桥接维护入口 | 已接维护准入、单端暂停、原子受理闸门及 local 占用保护；parallel 停止认领/宽限取消并禁用旧自动恢复，独立 recover-tasks 保留未决意图。真实进程/远端排空仍待验收 |
| 基线封存与生命周期 | V2026102907、四类范围 review/seal/activate、明确排除及同代次阻断已实现；别名迁移保留逐端证据、排除策略及审计，有占用时拒绝换目标 |
| 新接口和旧批次投影 | v2 文件 ID 处理/查询/取消、producer 查询/重试、回执分页、列表批量三端事实及预算设置已实现，契约 1.14.0；显式固定 legacy view 后支持旧歌曲/封面 process、进度/停止及封面字段。已知 metadata/backfill/retry/reset/recover 等治理入口保持原处理器与响应，受持久准入和 SQL 占用/skip 围栏保护；未知写入口仍拒绝 |
| 数据库迁移与后续边界 | 实际开发 crawler 已完成 V2912 扩展、历史补建及 V2913 物理收缩；同备份克隆与实际库37表保留字段逐值一致，31旧列/2索引为零，422份SQL prepare及32项相关回归通过。当前版本要求/执行证据见[迁移状态](../operations/resource-database-migration.md#v2913-物理云列收缩状态)，本次不启动业务或传输资源 |

当前维护工具一次只导入一个 owner 的双端声明；`status=2` 且来源身份及快照仍匹配的行可采信，其余行明确排除或审计冲突。四类 owner 的数据库范围、代次、目标及高水位均固定在清单中。双端导入 run 同事务推进，页完成后停在 paused，完整遍历为 completed；这些状态只表示导入进度，不表示基线已封存。执行中断后以相同摘要再次 apply 续跑，声明导入现提供独立import-control pause/resume/cancel，控制代次与每页进度分离；独立发现任务已提供 discover-start/run/status/pause/resume/cancel/export，按明确路径只读查询元数据，不能直接采信为 skip，见[历史发现](../operations/resource-history-discovery.md)。

## 1. 当前规则与本次目标

### 当前已实现行为

本分支新增的 MySQL 准入会先核对 schema 和持久维护状态；全局写入关闭时，写请求返回标准错误封装的 503，GET/OPTIONS、三个明确的只读 preview 和 tasks/stop 保持可用。local 暂停时旧歌曲/封面 process 与恢复入口同样拒绝。歌曲批次先检查准入及各项 skip，空队列和全跳过批次均不刷新 token；历史声明返回 `skipped/result_code=skip_existing`，不伪装成本次下载或核验成功。旧封面范围内，本地声明跳过后仍检查固定云端，只有两项均跳过才可结束为整项跳过，不能掩盖必需云端的缺失/暂停。以下列表记录 V2912 及更早的旧单端基线，用于说明迁移动机。V2913 保留旧接口和本地后单端云的处理顺序，但云端字段改由统一回执投影，四资源表不再物理保存 `cloud_*`。parallel 走统一持久任务，云端不等待 local，旧 process 经兼容层受理后也遵循三路独立语义。

- 封面由 `MetadataCoverService::processTarget` 先调用本地下载用例；只有本地处理成功，才调用云端用例。云端用例读取已确认的本地文件，再上传、核验和回写。依据：[服务](../../src/application/use_cases/MetadataCoverService.cpp)、[云端用例](../../src/application/use_cases/MetadataCoverCloudWorkflow.cpp)。
- [现有工厂](../../src/infrastructure/app/CloudFileStoreComposition.cpp)返回一个 `CloudFileStore`，由单一开关和 `baidupcs/rclone` 配置选择一个后端，未同时装配两端。
- 歌曲正式 filesystem 装配不自动上传；旧 rclone copyurl 是另一种资源保存模式。云端封面开关不会启用歌曲 uploader，见[组合根](../../src/infrastructure/app/ServerApplication.cpp)和[资源规则](../migu/resources.md)。
- 三张 resources 表各自保存一份最后云端位置；两张封面表另有独立的云端状态、代次和上传意图。切换上传后端会替换同一份回执。
- `song_resource_artifacts` 保存逐字 JSON、增强 LRC 等派生产物及其单份云端字段，目前没有自动双端上传。见[歌词说明](../migu/lyrics.md)。

### 本轮确定的目标

1. 配置启用本地、百度、Google 时，同一资源同时提交三项独立任务；任何一项的完成、失败、超时或取消不作为其他项的执行前提。
2. 通过一个资源传输工厂装配 `local/baidu/gdrive` 全部已启用目标，覆盖来源取得、本地下载和云端归档的依赖，分别配置并发、超时、重试和取消，保留全局资源上限。
3. 一张回执表覆盖歌曲资源、歌手封面、专辑封面和歌词派生产物；每个资源、每个平台保留一条当前记录。
4. `crawler` 既有歌曲按用户声明“两端均已保存”建立可审计的双端跳过基线，避免因旧 backend 单值而重复下载、上传。
5. 跳过传输、对象存在、内容已核验分别表达；旧校验值和用户声明不伪装成 SHA-256 实测结果。
6. 先通过独立库、替身和兼容验证，再安排生产升级；历史数据迁移、代码切换与删除旧列分阶段实施。

“同时提交”表示同批入队、有独立配额时立即开始，不承诺三个网络请求在同一时刻发生。共享的源站、网络、数据库和磁盘仍有物理限制；独立性指调度、状态、完成条件和失败处理互不串联。

## 2. 资源范围与身份

| owner_type | resource_id 指向 | 本期覆盖 |
| --- | --- | --- |
| `song` | `song_resources.id` | 每个音质的音频、歌曲图片及原始 LRC/MRC/TRC 等已登记资源 |
| `singer` | `singer_resources.id` | 歌手头像 |
| `album` | `album_resources.id` | 专辑封面 |
| `song_artifact` | `song_resource_artifacts.id` | `word_json`、`enhanced_lrc`，两种产物分别建回执 |

唯一身份为 `(owner_type, resource_id, cloud_provider)`；`resource_id` 是文件资源行主键，不是歌曲、歌手、专辑实体 ID，也不是派生产物的父 MRC ID。不同表的相同数字 ID 不能混用。`cloud_provider=baidu/gdrive` 表示存储平台；`baidupcs/rclone` 只作为工厂的 adapter 配置。

统一调度另使用 `destination=local/baidu/gdrive`，其中 local 不是 cloud_provider。云端回执仍只存两端，本地回执仍存资源表；统一工厂不意味着把本地文件也塞进 resource_cloud_receipts。一次具体工作还须携带内容代次、目标修订和所属操作身份，不能只用文件主键作为取消或重试的键。

不扩展任意文件归档、任意 URL 下载、播放器、账号平台或多账号编排。新文件类型必须先进入上述明确的资源模型；不能凭磁盘扫描结果直接生成未知所有者的回执。

## 3. 三路并行及内容来源

### 调度顺序

原方案“本地文件确认时才创建云端任务”改为“资源被明确提交处理时，登记本地与各云端任务”。元数据采集仍只登记资源，不因上传开关而自动发起下载；`process` 提交和明确配置的后续调度才启动三路。

~~~mermaid
flowchart TD
    Request[提交资源处理请求] --> Plan[校验身份 版本 配置与跳过策略]
    Factory[ResourceTransferFactory 装配来源与三个目标] --> Plan
    Plan --> Local[本地任务 独立认领与预算]
    Plan --> Baidu[百度任务 独立认领与预算]
    Plan --> Google[Google任务 独立认领与预算]
    Local --> LocalDone[本地完整提交]
    Baidu --> BaiduDone[百度核验与条件确认]
    Google --> GoogleDone[Google核验与条件确认]
    LocalDone --> View[汇总各目标状态与内容一致性]
    BaiduDone --> View
    GoogleDone --> View
~~~

任务登记通过同一短事务记录本次启用目标、资源版本、父操作及子任务，保证成功接收后不漏掉某一路；数据库登记失败则整次接收失败。网络、文件和上传不在该事务内执行。云端任务不要求资源本地 `status=2`，不使用本地下载的 claim_generation 认领云端。

三路任务在接收时各自登记一次。本地稍后完成只确认本地回执，不能再创建、重置或清空两端任务、取消标记和已成功回执；仅派生产物首次生成等新的明确处理计划才登记相应新任务。元数据事务不直接启动 I/O。

### 目标选择与幂等登记

- 新接口先按来源、资源身份和显式业务条件选择资源，再逐目标判断是否需要工作；不能沿用 `local.status IN (0,3)` 作为所有云端候选的前提，否则本地成功/跳过项无法补云端。本地状态筛选只作用于 local；云端状态筛选必须明确 provider，禁止隐式“任一端匹配”。
- 历史声明及本地跳过策略在任何来源解析、文件读取、摘要计算、远端探测或传输配额申请之前判断。全目标均跳过时直接持久化相应结果，不因缺少本地文件、token 或网盘在线状态而获取内容。
- `idempotency_key` 与首次接收的请求、冻结资源集合、目标集合、内容代次、目标修订及操作类型绑定；重试先读取原操作及其配置快照，不重新展开分页/缺省目标或重新选取当前资源。同键同请求返回原操作，同键不同请求返回 409；配置后来变化不把一次重试变成新传输。未提供键时仍以当前代次认领条件防止并发执行。
- 不同操作命中同一目标的活跃任务时返回该目标 `busy/not_started` 及可查询的原操作引用，不改写原 claim_generation、不隐式订阅或接管、不把 busy 算为已存储；其他空闲目标照常登记。取消只作用于本操作实际拥有的任务，避免一个批次取消另一个批次的工作。
- 普通 process 不清除已有 skip/cancelled 策略，不覆盖已满足当前策略的成功证据；明确 retry、修复、刷新才按各自边界生成新尝试。恢复终态操作时另建关联操作保留旧历史，不把原终态批次重新写成运行中。

### 各路如何获得字节

- 已有完整、校验匹配的不可变文件可被各路复用；读取期间通过文件租约或独占快照避免刷新、清理删除在途内容。
- 没有可复用文件时，本地任务获取并提交本地文件；每个云端任务通过已有音乐 provider/fetcher 独立获得内容，写入自己的受限临时区后上传，不等待正式本地文件或本地 DB 确认。
- 云端临时文件不登记为本地下载成功，不写入正式 local_path。百度、Google 的临时区、清理、超时和取消分别管理，失败只清理本次可确认无引用的临时内容。
- 正式对象路径由既有路径规划能力结合资源身份、类型/音质和内容代次确定，不能再要求先取得正式 local_path 才能规划云端路径。新 parallel 对象使用代次/内容摘要限定的不可变路径和独占尝试文件，避免旧 worker 即使被 DB 拒绝仍覆盖新对象；不能让旧任务先覆盖稳定正式路径、再靠条件回写“补救”。上传意图保留各目标的实际计划；历史对象沿用核实路径，不在迁移时统一改名。
- 初期采用“已有快照复用，否则独立获取”，不引入必须等待共享下载完成的串联步骤，也不引入一个慢消费者就阻塞其他路的共享流广播。没有快照时可能产生多次源站读取，纳入全局出站预算和验收。
- 大音频使用有界流式读取、摘要计算和临时文件，不能把现有封面的完整 `bytes` 模型直接套到所有音频；内存、临时盘和文件句柄均设置上限。
- 临时歌曲 URL 每次尝试按现有 listen/token 规则重新取得，不能长期持久化或跨重试复用；永久版权/购买限制继续保护。某一路本地 I/O 失败不影响其他路，明确的共享来源禁用或永久限制则阻止新的源站获取，已确认副本保留。
- 所谓互不依赖不取消“必须先获得合法内容才能上传”的前提。既有资源被跳过时不为制造快照而重新请求源站；来源获取只服务确有传输需求的目标。
- token 刷新可经现有线程安全 port 合并同一刷新请求，单个目标取消只结束自己的等待，不能清空其他目标有效 token 或取消公共刷新。源站永久限制与单个目标 I/O 失败分开；云端失败不得调用本地队列 markFailed/markSkipped 去覆盖本地状态。

### 版本与三端内容一致性

三路独立获取可能遇到上游内容变化，因此“同一资源 ID”不能直接等同于“同一字节”。拟在四类资源中维护内容代次 `content_generation`、可空的参考 SHA-256/大小；本地文件及云端最后确认记录分别保存自身所属代次。参考快照独立于本地下载成功状态。

1. 新处理沿用当前代次；明确刷新内容时递增并清空新代次参考摘要，旧文件及回执保留自己的代次和摘要。单路 retry、补建回执和修复某个目的地不递增内容代次。
2. 有可靠参考摘要时三路分别比较。无参考摘要时，第一个完整、合法内容快照可通过条件更新绑定参考摘要，不要求该路已完成正式本地发布或云端上传；绑定同时核对活跃认领、取消状态和来源描述修订，已失效 worker 不能抢写参考摘要。
3. 其他路不等待该路完成；提交时核对当前代次、参考摘要和自身字节，发现不同内容记 `content_conflict`，保留原成功回执，不自动覆盖或重试直到“碰巧一致”。
4. `current` 依据当前内容代次、目标修订及所需核验强度计算，不再把本地成功作为必要条件；另返回 `matches_local=true/false/null` 和跨目标一致性，缺少本地文件或证据时为未知。
5. 各资源表现有 `checksum/size_bytes` 仍描述本地保存结果，不能被云端先完成的字节覆盖。参考摘要、任务期望快照、最后成功回执分开保存。

目录重采更新图片 URL 等来源描述时，不自动重下已完成文件；在途目标仍须比较取得内容时的来源描述修订，不能拿旧 URL 的结果确认新描述。需要重新获取新内容时明确重规划/刷新。比较内容代次和摘要、绑定参考快照均在正式发布/上传前完成；发布后的 DB 确认再次比较，以覆盖在途刷新。

## 4. 工厂、配置和独立执行

### 工厂职责

采用上层 `ResourceTransferFactory`，一次装配 local、baidu、gdrive；返回 `ResourceTransferAssembly`，包含来源读取/路径规划的窄 port、按 destination 索引的传输策略、执行预算和目标修订。工厂只构造和校验，不下载、不上传、不领取数据库任务、不在构造阶段发起远端健康探测。

统一工厂是三个完整传输流程的组合入口，不能仅统一三个“写文件对象”后漏掉本地来源获取。各传输策略通过同一小型执行契约接受资源计划和尝试上下文、返回逐目标结果；底层存储和确认接口按职责保留，不让本地实现伪造网盘路径或远端核验方法。

| 工厂装配项 | 依赖与执行职责 | 确认归属 |
| --- | --- | --- |
| `local -> LocalResourceTransfer` | 已有音乐来源 port、路径规划、歌曲流式 blob store、封面保存/读取及派生产物本地发布；复用现有能力 | 四类资源各自的本地队列/文件回执 |
| `baidu -> BaiduResourceTransfer / RcloneResourceTransfer` | 按 adapter 选择原生进程/私有回读，或 RC 流式 multipart/远端核验；独立百度预算 | provider=baidu 的云端回执，adapter 独立记录 |
| `gdrive -> RcloneResourceTransfer` | 来源租约文件、按需 HTTP multipart、远端 hash/只读回读、Google 预算 | provider=gdrive、adapter=rclone 的云端回执 |

`ResourceTransferFactory` 返回本地保存器及三端 ResourceTransferDestination，保留逐平台装配错误；构造不发请求或创建目录。ResourceTransferComposition 在显式 parallel 模式分别装配来源、身份围栏、worker 和预算；真实平台验收仍待执行。只有 baidupcs 策略要求独立回读临时根；百度选择 rclone 可复用既有 RC 与 WebDAV 登录。CloudFileStore 的 bytes 工厂保留给旧封面兼容。

平台与工具必须分别配置：百度允许 baidupcs 或 rclone，Google 当前使用 rclone。原 RC 地址、认证与 remote 配置直接复用；逐端覆盖不修改另一端，同一 RC/root 不可同时冒充两个平台。旧单端入口使用 `CRAWLER_RESOURCE_CLOUD_PROVIDER` 声明实际平台；legacy rclone 未声明 baidu/gdrive 时启动装配拒绝，不能先上传再猜 Google；baidupcs 固有归属百度。登记在既有 `request_snapshot.adapters` 冻结工具，幂等重放和排队任务不随配置换工具；旧记录缺工具保持未知，旧未决对象先由原工具 repair，再切换。核验强度按实际工具与证据判定，不能只按平台判定。实现边界见[配置复用与工具冻结](../operations/resource-transfer-tasks.md#配置复用与工具冻结)。

- 来源 port 只取得受支持音乐资源，存储 port 只做完整对象写入/核验，repository 只做条件确认；工厂和 API 均不承载这些业务步骤。
- 现有 MetadataCoverStore 合并了图片获取与本地保存，不能直接作为云端独立来源；需提取可复用的受限图片 fetcher/校验 helper，再由本地保存与两端临时快照分别使用。云端装配不要求先创建可写正式 FileStorage；BaiduPCS 临时根也不能偷用已关闭 local 的正式根。
- 云端上传输入改为受控的不可变文件/流快照描述，携带大小和摘要；大文件不要求复制成整段字符串。来源取得由用例经窄 port 编排，不让存储 adapter 自行拼接音乐平台请求。
- 各目标配置、装配结果和健康状态独立；某一云端认证缺失或本地根不可写不导致另两路被短路。全局配置结构非法则拒绝整份配置；单端错误保存在该端装配结果中，不能漏掉或静默替换目标。
- rclone 可以连接多种平台，`gdrive` 的平台/目标身份必须来自已核实配置或独立只读探测，不从 remote 名称推断。新的未知目标在显式探测通过前不可传输；历史 skip 可凭匹配的持久目标修订返回，不强制在线探测。认证只在 infrastructure 配置内，不写入回执、批次或日志。
- 一个目标可以复用经证明线程安全的底层客户端，但不能共享可变请求、取消标志、未隔离临时文件或一个串行全局上传锁。共享 blocking executor 也须为各路保留可执行能力，不能被慢上传进程占满。

### 本地下载必须保留的完整链路

1. 先检查本次 local 是否启用、历史跳过、幂等/活跃任务和资源身份；跳过即返回操作结果，不尝试读文件“证明可以跳过”。本地禁用时工厂不打开正式文件根，云端仍可独立运行。
2. 在 local 队列认领自己的任务，固定 content_generation、来源描述和本地 target_revision。保留歌曲永久版权/购买限制；不通过重置 status=4 来适配新工厂。
3. 取得已核验的不可变来源快照，或通过既有 provider 换取本次临时 URL 后流式获取；保留 TLS、重定向、响应上限、超时和取消检查。来源失败只结束所属目标，明确的资源级来源限制另按规则记录。
4. 规划根目录内的安全路径，打开独占 staging/write session，逐块写入、累计大小和 SHA-256。复用 [AsyncSongResourceContentFetcher](../../include/crawler/application/ports/provider/AsyncSongResourceContentFetcher.h) 与 [AsyncSongResourceBlobStore](../../include/crawler/application/ports/storage/AsyncSongResourceBlobStore.h) 的职责；图片继续验证实际类型，生产大文件禁止回退到内存聚合替身。
5. 完整传输及内容校验通过后比较/绑定参考快照，才 commit 完整文件；部分下载、超限、取消和摘要冲突不发布为成功文件。parallel 的实际发布路径使用独占版本快照，沿用已有文件会话的 require_database_ack 隔离，不能回退为多 worker 直接覆盖同名文件；逻辑文件名的兼容见第 8 节。
6. 同一短事务核对 local 认领代次、内容代次、来源及本地目标修订，再写本地文件回执；不修改已经独立执行的云端任务。只在该确认成功后触发与本次已发布 MRC 关联的后处理，避免云端分支重复调用原始后处理钩子。
7. DB 明确确认后 accept 并按引用清理旧文件；条件拒绝只回收本次确实无引用的产物；提交结果未知时 defer，保留文件及 journal，重启后按所有者/版本/引用核对，不能当作已回滚。
8. 逐目标结果汇入操作，再供本地 content/目录接口读取；本次 local 未执行/失败不撤销旧已确认文件。available 依据原可用性规则计算，新版未探测可为 null，不能把 skip 等同于文件丢失，也不因云端成功填造 local_path 或本地状态。历史 skip 不读内容；旧接口确有布尔可用性字段时，仅按原根边界/存在性/大小规则做元信息检查，不触发下载或完整 hash。

### 当前配置与后续边界

当前配置通过进程环境/私有配置读取，默认保留 legacy；显式 parallel 启用独立 worker，新写入口还要求 schema 和维护闸门就绪。下表只列当前支持的变量，完整逐端范围见[配置说明](../configuration.md#parallel-三路运行)。持久队列容量和旧视图投影不提供假开关。

| 拟议配置 | 默认与规则 |
| --- | --- |
| `CRAWLER_RESOURCE_PIPELINE_MODE` | `legacy/parallel`，升级默认 `legacy`；本方案三路需设为 `parallel` |
| `CRAWLER_RESOURCE_LOCAL_ENABLED` | `true`；允许关闭正式本地保存，云端仍可使用独立临时区 |
| `CRAWLER_STORAGE_PATH` | 沿用本地正式文件根；parallel 的 local 固定为 filesystem，旧 rclone copyurl 不能冒充本地目标 |
| `CRAWLER_RESOURCE_CLOUD_PROVIDERS` | 空；`baidu,gdrive` 同时启用两端，顺序不表达依赖或成功优先级 |
| `CRAWLER_RESOURCE_BAIDU_ADAPTER` / `CRAWLER_RESOURCE_GDRIVE_ADAPTER` | 分别为 `baidupcs` / `rclone` |
| `CRAWLER_RESOURCE_{LOCAL,BAIDU,GDRIVE}_CONCURRENCY` | 花括号代表三组独立变量；每路默认 3，范围 `1..32`；每路 worker/信号量独立 |
| 同前缀 `_TIMEOUT_MS` / `_RETRIES` | 三端默认 120000，范围 `100..600000`；重试默认 1，范围 `0..5`；旧接口的独立参数范围保持 |
| 同前缀 `_MAX_BYTES_PER_SECOND` | 默认 0 表示该路不额外限速；仍受全局上限；正值按实际配置限制 |
| `CRAWLER_RESOURCE_SOURCE_CONCURRENCY` | 默认 6、范围 `1..32`；限制三路合计源站请求，不被各路并发相加绕过 |
| `CRAWLER_RESOURCE_TRANSFER_CONCURRENCY` | 默认 9、范围 `1..96`；约束本地传输与两端云操作总量，公平轮转避免某一路占满 |
| `CRAWLER_RESOURCE_{BAIDU,GDRIVE}_STAGING_PATH` / 同前缀 `_STAGING_MAX_BYTES` | 花括号代表两组独立变量；需显式配置可用根和正字节上限；各路独立预留，空间不足只暂停该路的新认领 |
| `CRAWLER_RESOURCE_CLOUD_VERIFICATION_POLICY` | `hash_match` 或 `strict_sha256`，默认前者；核验含义见第 7 节 |
| `CRAWLER_RESOURCE_SHUTDOWN_GRACE_MS` | 默认 30000、范围 `0..600000`；到期未确认任务保留可恢复状态，不伪造成功 |

最小三路配置示例（认证、目标根、临时区和基线仍需按部署流程准备）：

~~~dotenv
CRAWLER_RESOURCE_PIPELINE_MODE=parallel
CRAWLER_RESOURCE_LOCAL_ENABLED=true
CRAWLER_RESOURCE_CLOUD_PROVIDERS=baidu,gdrive
CRAWLER_RESOURCE_LOCAL_CONCURRENCY=3
CRAWLER_RESOURCE_BAIDU_CONCURRENCY=3
CRAWLER_RESOURCE_GDRIVE_CONCURRENCY=3
CRAWLER_RESOURCE_SOURCE_CONCURRENCY=6
CRAWLER_RESOURCE_TRANSFER_CONCURRENCY=9
~~~

目标、adapter、账号/盘/根、临时区、并发及核验策略均为启动配置。v2 settings 可调整四项预算（超时、重试、大小、限速），只影响之后受理的操作，排队/在途任务继续使用首次冻结值，不回写私有文件。请求只选择已启用目标，当前不接收逐请求预算覆盖；旧参数范围保持原合同。认证和路径不由 HTTP 请求提供。

legacy 模式才按旧 `RESOURCE_CLOUD_UPLOAD_ENABLED/BACKEND` 解释原有单端能力；parallel 模式只使用显式新 providers 列表，不把旧封面开关自动扩展为歌曲上传，也不合并两套配置。请求省略目标时使用本次配置快照；未知目标或预算越界返回参数错误，请求的目标未启用则返回不可用，全部目标关闭时拒绝处理。已启用但暂时不可用的单端以该端失败结果登记，其他有效目标仍可接收；整次 DB 登记保持原子，不把单端运行错误当成 DB 接收失败。

BaiduPCS-Go 的内部并行/重试与外层 worker 是两个层次，需共同计算有效并发和总尝试上限；rclone RC 同样受每端和全局配额约束。所有等待均可取消，网络等待不持有数据库连接或长事务。

parallel 模式对每个启用目标保留至少一个执行配额，源站与传输全局并发上限不得小于本次需要相应配额的目标数；不足时明确拒绝配置，不悄悄退化为本地占满配额、云端长期等待。每路内部仍按自己的并发上限执行。

来源获取、临时区分配和云端上传分阶段申请可取消配额，按固定顺序取得并及时释放；不能持有全部上传/数据库配额等待来源配额而形成循环等待。三路均 skip 的请求不需要取得传输配额。

### 暂停、取消和恢复

- 每个资源的本地、百度、Google 分别具有 item 身份、claim_generation、超时与取消标记；父批次只汇总，不把某一子项失败传播为其他子项取消。
- 目标级暂停只停止新认领，在途操作继续收尾；恢复后只领取尚未终止的有效任务。某端重试等待不占用其他端配额。
- 子任务取消只针对选定目标；整批取消才向该批次全部目标传播。单次请求超时不取消另外两路，客户端断开不默认取消已持久化后台任务。
- parallel 不能直接沿用现有 BatchTaskManager 的共享 task_timeout_ms/停止标记作为各目标超时。单目标预算只影响自身；如需整操作截止时间，须显式作为全目标取消策略配置。排队/暂停与实际尝试耗时分别记录，不能以兼容视图超时自动取消额外平台。
- 已排队取消项不再获取内容；在途项尽力停止自身 I/O。远端已完成但响应不明时记 `uncertain`，保留意图供核对；取消不删除远端对象、不撤销其他目标成功。
- 显式取消后普通自动 worker 不重新领取；需要明确 resume/retry 指令，生成关联的新操作/尝试，不复活旧终态批次。超时恢复只处理各目标自己的认领，推进代次，旧 worker 回写必须被拒绝。
- 关闭服务先停止认领，在宽限内收尾；到期保留未决意图和必要临时文件。重新启动核对持久任务，不能仅凭内存父批次终态认定三路排空。
- 各 worker 持有必要 port 和不可变操作上下文，不捕获工厂、兼容批次管理器或 controller 的裸地址。兼容视图完成不能释放另两路仍在使用的对象；关闭时先协调 worker/journal 再取消数据库池，超时未决不能被当作全部排空。

## 5. 回执与持久化约束

### 记录职责

`song_resources/singer_resources/album_resources/song_resource_artifacts` 保留资源归属、本地状态及文件快照，以及必要内容代次与参考信息。当前云端读写已经迁到 `resource_cloud_receipts`；V2913 精确封存并删除这四表31个旧物理云列，含派生产物2列。HTTP 同名字段继续作为新表投影，不随数据库列删除。

| 信息组 | 必须表达的内容 |
| --- | --- |
| 身份 | owner_type、resource_id、cloud_provider，三列联合唯一 |
| 当前任务 | 期望内容代次、目标修订、状态、claim_generation、批次、认领时间、重试时间、上传意图及脱敏错误 |
| 最后确认回执 | 对象路径、大小、规范 SHA-256、实际远端 hash 算法/值、所属内容代次和目标修订、确认时间 |
| 证据 | 核验级别/方式、来源、采信时间、迁移 run/manifest 标识、可空的实际核验时间；旧视图需要的实际上传 adapter 作为来源证据保留，不作为平台身份键 |
| 跳过策略 | 跳过原因及绑定的内容代次/目标修订；不能因为缺摘要而重新归入普通上传候选 |

当前任务状态与最后成功回执独立；新上传失败、取消或未确认时保留上次确认对象。拟使用 `waiting_input/pending/running/succeeded/failed/uncertain/cancelled/paused/skipped` 等明确任务状态，不把“历史跳过”塞进旧歌曲的版权 `status=4`，也不占用旧云端 `4=待核对` 的含义。

waiting_input/pending/running/paused 不视为完成；succeeded/failed/cancelled/skipped/uncertain 表示该次尝试结束，uncertain 仍须后续核对，不能表示远端副作用已停止。只有全部请求目标本次尝试结束，真实父操作才终止。全 skip 返回 all_skipped 和核验成功数 0；busy 单列为未执行，不能计入成功。取消请求已收到、网络正在收尾与实际排空分别表达。

不在 resources 重复持久化 `baidu_stored/gdrive_stored`。API 从回执及证据派生；上传重试更新当前行，尝试明细继续进入既有批次和事件。历史基线的用户声明可保留在证据字段中，不依赖逐次网络请求才能跳过。

### 引用完整性及事务

MySQL 8.0 对涉及外键级联动作的列与 CHECK 有组合限制，不能按原稿直接给四个级联外键列增加归属 CHECK。拟保留四个可空的真实资源外键和 ON DELETE CASCADE，由回执表 BEFORE INSERT/UPDATE 触发器校验“恰有一个归属，且与 owner_type/resource_id 一致”；联合唯一键仍防止重复回执。触发器仅检查本行身份并拒绝非法写入，不调度任务或执行业务流程。约束依据见 [MySQL CHECK 限制](https://dev.mysql.com/doc/refman/8.0/en/create-table-check-constraints.html)与[触发器语义](https://dev.mysql.com/doc/refman/8.0/en/trigger-syntax.html)。

删除清理由原生外键完成，不依赖子表 DELETE 触发器；MySQL 外键级联不激活触发器，见[外键说明](https://dev.mysql.com/doc/refman/8.0/en/create-table-foreign-keys.html)。触发器存在性/权限、非法直接写入、upsert、来源级联和迁移重跑均列为上线门槛，不能只靠 adapter 约定代替约束。

[SQL 分句器](../../src/infrastructure/music/persistence/sql/SqlFileLoader.cpp)已为迁移提供独立的 DELIMITER 支持，使复合 CREATE TRIGGER 作为完整语句发送；DELIMITER 不发送服务器。普通运行期 SQL 保持原分句规则。DDL 权限、中断和恢复仍须按部署矩阵验证，不跳过约束或伪造迁移记录。

- 每次本地确认或云端确认，比较资源内容代次；云端另比较本平台 claim_generation 和目标修订。统一锁顺序为资源行、当前目标配置行、回执行；不在锁内执行网络 I/O。
- 多资源/多目标写入按固定 owner_type/id 和 destination 顺序加锁；死锁/锁超时只有限重试幂等的 DB 事务，不通过重试整个用例重复下载或上传。
- 处理接收事务原子登记三路计划；本地完成事务只确认本地文件，不能重新排队云端。真正改变内容代次的刷新、派生首次生成等事务负责其相关任务登记/失效，须使用同一数据库连接；不能通过多个各自获取连接的 port 调用声称原子性。
- DB 响应报错后的重读只能确认已经看见的匹配结果；未命中或不同快照仍是未知。保留文件、上传意图和日志，不能据此删除对象。
- 设置 `resource_destination_states`，按 local/baidu/gdrive 各保存一条当前目标状态、持久 target_revision 和不含凭据的身份指纹；回执及在途意图携带各自修订。本地正式根变化也须使旧 worker 失效，不能只为云端做保护；该表不引入多账号/多盘 target_id 编排。
- 临时文件 journal 绑定 owner_type、resource_id、内容代次、provider 和认领代次。清理依据引用和确认结果，不凭任务失败或文件年龄批量删除。

## 6. crawler 历史基线与补建

### 已有歌曲的明确处理

用户确认：`crawler` 库既有歌曲的旧字段可能写 `baidupcs` 或 `rclone`，但两端均已保存。本次规划把这一信息作为该库、指定历史资源集合的人工采信依据，不能只按旧 backend 迁一端，更不能推广为其他库或新资源也已双端保存。

历史接管的默认动作是 `import_attested_skip`：按清单为每个确认范围内的歌曲文件建立百度、Google 两条记录，标记 `task_state=skipped`、`verification=operator_attested`、`skip_reason=legacy_both_clouds_present`，保留用户说明和清单摘要。不请求源站、不下载远端对象、不重复上传；本地缺失也不因此自动重下。

`operator_attested` 表示用户已确认保存，`content_verified` 表示达到核验策略；两者分开。可以返回 `assumed_stored=true/transfer_skipped=true`，但不能填造 SHA-256、uploaded_at、已验证路径或 `current=true`。未提供的另一端路径允许为空，记录 `path_unresolved`；正常 worker 不把它当作缺失对象自动修复。

这项跳过绑定历史资源集合、内容代次与目标修订，不是全库永久关闭下载。新增资源、明确刷新后的新内容或新账号目标不继承。既有歌手/专辑封面、派生文件是否已双端保存需独立清单，不能从“歌曲已存两端”自动推定。

本地默认跳过也必须持久化为资源级策略，包含绑定的内容代次、baseline 标识和原因，与本地文件状态、版权跳过分开；不能只创建两条云端 skip 后让本地 worker 重新下载。调度批量读取该策略，重启、续跑和普通 process 均遵守；明确修复 local 或刷新内容才解除对应范围的抑制。

skip 是独立传输策略，不是一次探测结果：后续 probe 失败、摘要不足、hash 策略变严或记录 current=false，均不能把 skip 改回 pending。只在明确 repair/刷新/对应目标切换操作中解除指定目标的策略，并保留变更审计。导入不是将任务批量标为 succeeded；旧版权 status=4、活动认领和既有强证据分别保留。

本地 skip 绑定本地目标修订，两端 skip 各自绑定其云端修订；改变 Google 目标不解除 local/百度 skip，更换本地根不解除两端 skip。仅为补充可空的路径或摘要也不解除 skip。

### 分步建立迁移清单

1. 先只读盘点 `crawler` 的资源与派生产物、旧状态/路径/hash、来源身份和迁移版本，记录实际时间、最高主键和各类计数；只读不意味着本轮已取得数据库访问授权或已执行。
2. 为每类表固定 `high_water_id`，导出不可变 ID 清单、内容代次/旧值摘要及稳定来源身份指纹（平台 ID、资源类型/音质、派生版本等）。初始歌曲范围以确认为已保存的文件行及用户声明清单为准；待下载/失败行、无有效来源行和异常指纹单列，不因数据库名相同全部打成功。最高主键本身不能证明 ID 仍归同一文件。
3. 明确百度/Google 当前目标身份和根；旧 `baidupcs` 已确认位置可映射百度，旧 `rclone` 位置仍需配置/清单确定平台；用户双端声明只补存在声明，不替另一端制造路径。
4. 通过数据库一致性快照建立独立克隆库，校验表/视图没有跨回源库的引用；历史清单在克隆库演练。复制凭据和敏感 URL 均不进入日志或清单。
5. 输出 dry-run：每平台预期新增/复用/待核对/冲突/跳过数量，待保留旧值，以及预期源站下载数、远端内容下载数、上传数。默认历史导入这三项网络计数均为 0。
6. 演练后固定 manifest 的数据库身份、schema/源码版本、ID 范围、当前内容/目标代次、证据、策略、排除项和文件摘要。正式执行使用同一清单，变更范围先重新预览。
7. 在旧写入器已停止、桥接版本自动调度被维护闸门关闭后，分批幂等插入两端记录及本地 skip；每页重读并核对清单中的身份、代次、目标修订和旧值摘要。现有更强核验、新内容代次、活跃认领不得被弱历史声明覆盖；变化/消失的行进冲突清单，不强行导入。正式生产导入顺序按第 11 节，不能边运行旧 writer 边假定清单静止。
8. 核对预期与实际记录数、唯一性、引用及 skip 原因；重跑同一清单应新增 0 条、不降级已有证据、网络传输 0 次。源数据继续变化时按清单边界及后续增量分别统计。

旧值在 `resource_cloud_migration_audit` 中按 run、owner_type、resource_id 留存可恢复的迁移快照；只保存必要字段和脱敏身份，不保存认证或敏感临时 URL。不能识别的平台、路径或校验值进入该留存和待核对清单；完成这些留存并验证可恢复后，才允许进入删列阶段。

### 跳过、核验和修复的操作边界

| 模式 | 读取对象元信息 | 下载文件内容 | 上传/覆盖 | 使用场景 |
| --- | --- | --- | --- | --- |
| `import_attested_skip` | 不要求 | 否 | 否 | 本次 crawler 歌曲默认接管 |
| `probe_existing` | 是，有界只读 | 否 | 否 | 补路径、存在性、大小和远端摘要；失败保持待核对 |
| `verify_content` | 是 | 仅明确选择的对象可回读 | 否 | 提高核验等级，不隐式修复 |
| `repair_missing` | 是 | 明确范围内获取必要来源 | 仅确认缺失的目标 | 用户明确选择修复后补缺 |
| `replace_conflict` | 是 | 按已审阅清单 | 仅明确对象 | 内容冲突另行处理，不作为普通重试 |

普通 process/retry 尊重已导入 skip；核验失败、文件缺失或 hash 不足不得自动切换成 repair。HTTP 超时、认证失败、未知元信息不能当作确定缺失。同名不同内容默认报告冲突，禁止盲目覆盖。原本已完成本地状态不批量 reset，缺失本地文件返回不可用及跳过原因；用户明确修复 local 时可从已核验云端副本恢复，不能默认为重新请求音乐平台。

dry-run 只读数据库和已提供的清单，不写正式回执/任务、启动 producer 或发起联网核验；需要远端探测时另建显式 probe run。默认 import_attested_skip 不读取本地文件计算 hash，也不强制登录网盘，避免第 7 节的可选核验动作变成跳过前置条件。工厂在这一路径只提供已配置目标描述，不执行健康 I/O。

### 新启用平台及运行中的增量补建

补建控制状态拟存 `resource_cloud_backfills`：run/manifest、数据库身份、owner_type、provider、target_revision、high_water_id、last_committed_id、策略、统计、状态和脱敏错误。批次/items/events 复用现有机制报告进度，不为千万条历史 skip 每条制造常驻内存任务。

- 启用一个平台时，新 process 提交的资源生成其任务；仅有元数据新登记仍不发起 I/O。已有资源创建显式补建 run，默认先 dry-run。首次启用不自动把旧库全量排入上传。
- 识别出匹配的历史双端基线时复用 skip；真正新平台或新目标没有这份声明，先 `probe_existing`，已核验匹配则复用，未知保持待核对。缺失上传通过明确 `repair_missing` 计划处理。
- 每类表按主键 `id > cursor AND id <= high_water_id` 遍历，批大小默认 500、范围 `1..5000`，不用不断增大的 OFFSET。唯一键保障重复扫描不重复插入。
- 一页的有效回执、本地 skip、逐 ID 冲突/失败清单和 checkpoint 推进在同一事务提交；中断从最后已提交游标继续。探测 I/O 在页事务外，写入时重新核对身份与版本；没有持久化失败 ID 的页不能仅推进游标把失败丢掉。
- 高水位之后的资源在明确 process 时登记所选目标，后续历史巡检另建新高水位 run；补建期间明确刷新旧资源同样增量登记。仅有元数据新增不自动产生三路传输。切换上线前追加缺口巡检，覆盖旧代码尚未捕获变更的窗口。
- 暂停、取消和限速按 run/provider 生效；只取消本 run 创建且拥有的工作，不扫掉同 provider 其他批次。暂停可原地继续；终态取消后 resume 建立关联续跑 run、沿用已提交 cursor，旧取消记录保留。执行前核对目标修订与 manifest 未变化；部分失败 ID 必须可追踪。
- 日常领取 pending/retry 到期任务走 provider、owner_type、状态、下次执行时间和 ID 索引；发现缺失行、完整对账和精确盘点用独立分批任务，不混入每轮上传认领。

## 7. 歌曲旧校验值是否需要重新获取

不要求一律重新下载歌曲。先识别算法与证据，再利用现有文件或只读元信息；只有明确要求更强核验、且没有可比对证据时，才回读云端内容。重新请求音乐源站属于最后的显式修复动作，不是迁移前提。

下表是明确执行 probe/verify 时的核验选择，不是 import_attested_skip 的额外步骤。默认历史接管先保存原算法/证据并跳过全部内容 I/O；不得为补齐参考摘要或 current 标记突破跳过策略。

| 历史情况 | 默认动作 | 能证明什么及后续 |
| --- | --- | --- |
| 合法本地 SHA-256 且文件可读 | 有界读取本地文件核验；不请求源站 | 本地完整内容；不能单凭它证明两端对象相同 |
| `rclone:sha256:<hash>` 等带算法值 | 保留算法/原值，核对对象身份及当时证据再规范化 | 未核对来源前不标新核验时间；不能仅改前缀宣称实测 |
| `rclone:md5:<hash>` / SHA-1 | 单独保存远端摘要；本地可计算同算法比较 | 不能将其写入 SHA-256 字段；需要时同时计算本地 SHA-256 |
| `rclone:size:<bytes>`、仅大小/路径或空值 | 用户历史范围按声明跳过，核验状态保留弱证据 | 相同大小不能证明相同内容；可先只读探测后另选回读 |
| 没有本地文件、两端已声明存在 | 保持两端 skip，允许补路径/元信息 | 不为了补 DB hash 自动重下歌曲；严格核验可流式回读已存对象 |
| 本地与远端或两端摘要矛盾 | 记 conflict，保留原值和对象 | 不自动覆盖，不用重试掩盖；检查版本、账号、音质和路径归属 |

拟定证据等级为 `unverified/operator_attested/object_observed/hash_matched/sha256_verified/conflict`。时间分别记录导入时间、声明时间和实际核验时间；迁移当天不是历史上传时间。

`hash_match` 策略允许远端 SHA-256，或本地同一完整字节快照的 MD5 与已核实对象 MD5 匹配；记录真实算法和方式，同时保存本地计算的规范 SHA-256。`strict_sha256` 要求远端可信 SHA-256 或云端流式回读后计算 SHA-256，MD5 一致不能冒充满足严格模式。平台 hash 缺失或特殊对象没有可比摘要时，不降低配置要求悄悄成功。

源站内容可能已变化，不能把新下载的音频不经版本判断就用来否定或覆盖历史归档。核验升级独立运行，可以逐批提高证据强度，不改变历史默认跳过策略。

## 8. song_resource_artifacts 的接入

- 本期一并迁出派生产物的 `cloud_backend/cloud_path`，以 `owner_type=song_artifact` 接入同一双端回执；每个 `word_json/enhanced_lrc` 使用自己的 id、大小和 SHA-256，不共用父 MRC 回执。
- 内容版本同时受 `source_resource_id/source_checksum/processor_version/schema_version/artifact_type` 约束。来源或处理器变化时，使两端旧回执失去 current，保留原成功记录直到新版本确认。
- 派生产物必须先有可读取的输入并完成解码/导出；这属于生产者的真实输入依赖，不能在 JSON 尚未生成时上传。生成不可变产物快照后，本地发布与两端归档分别调度，不以正式本地路径确认成功作为两端归档前提。
- 复用现有解码/导出能力，将“生成临时不可变快照”与“正式本地发布”分开；不能原样调用会先写正式根的旧保存流程作为两端上传前置。每个网盘不重复解密，producer 临时区独立于 local 正式根。无可用 MRC 或密钥时报告前置条件不足，不重置原始 MRC 或虚构产物；云端来源读取按明确恢复计划接入窄 reader。
- 保留逻辑文件名 `words.json/words.enhanced.lrc`、内容协议和 legacy 的固定路径行为；parallel 发布采用版本隔离的物理快照并由 DB 回执定位，上传不能读取正在被覆盖的固定路径。两个产物同版本发布/确认、备份补偿及旧版本引用保护继续成立。
- 已有封面 content/目录 HTTP 接口仍经资源身份定位实际回执，旧历史文件不批量改名；该原则不意味着本期额外新增派生文件内容路由。直接依赖固定磁盘路径的离线工具/外部脚本必须在切换前迁移到回执路径，或继续使用 legacy；若提供固定路径导出，它是显式兼容副本而非三路的权威内容，需单独串行发布和验证，不能默认为不可变快照已经兼容旧脚本。
- 两端回执分别失败/重试，派生失败不回滚原始歌曲/MRC 的成功；旧歌曲双端声明不自动覆盖其派生产物。历史派生归档单独盘点，已有文件可登记/核验，不为补云端索引重下原始 MRC。
- 新生成计划先为两类产物登记稳定资源身份及 producer 版本，再登记目标任务。未生成时目标为 waiting_input，仅等待对应 producer，不等待 local 目标；producer 按来源内容/处理版本幂等，输出具有引用保护的临时快照。local 关闭或失败时，显式派生计划仍可从受控来源快照生成后供两端归档。
- 普通 MRC process 受理时与显式 produce_artifacts 共用持久 producer 计划和幂等登记；下载来源及 local 成功钩子不再同步生成，不能分别生成、覆盖和排队两次。历史原 MRC skip 不隐式启动 producer、回读 MRC 或清除其跳过策略；需要补派生内容时单独明确该范围及来源。

## 9. 刷新、删除、别名合并与目标切换

### 刷新和局部修复

明确 `refresh_content` 时推进内容代次，撤销旧代次活跃认领的回写资格，只为本请求选定目标登记新任务；省略目标时才使用已启用配置快照。未选中目标的旧回执可失去 current，但不因此自动排队传输。各路旧成功文件和回执保留为上次结果，不立即删除；刷新失败不丢失旧可用副本。

`retry` 只重试指定目标当前代次；`repair_destination` 只修复缺失/损坏目的地，可复用已核验副本，不推进其他两路版本或要求它们重传。修复产生与参考摘要不一致的内容时报告冲突。

### 删除

四类资源删除及父来源级联删除由真实外键清理回执；正在执行的 worker 在确认时必须因资源不存在/代次不符被拒绝。派生 MRC 删除同时清理产物及其回执。

删除数据库记录不自动删除网盘对象。远端清理由单独可审阅清单执行，区分资源移除、别名迁移和内容历史；DB 结果未知、仍被规范资源引用或仍有在途意图时，不删除本地/云端对象。维护工具若绕开常规 writer，也要通过约束验证引用完整性。

### 专辑别名及资源归一

1. 同事务锁定别名与规范资源，确定保留的本地文件、内容代次及目标修订，先取得新资源主键。
2. 分平台处理两端回执：与保留内容匹配的强证据可迁入；不同内容、目标或声明强度冲突进入审计，不把两端不相容回执拼成“全部成功”。
3. 两边同平台都存在时，按同代次/同目标匹配、核验强度、实际确认时间选择；不能仅按旧 updated_at 或“最后一行”覆盖。
4. 活跃任务不继承旧主键的运行态；取消或推进旧代次，对新主键创建明确任务。先迁移有效回执、再删除别名，避免外键级联提前丢失。
5. 提交后按文件引用决定本地清理，旧 worker 不得用旧主键失败重读推断对象未被迁移。已有[别名流程](../../src/infrastructure/music/persistence/mysql/MySqlMetadataAlbumAliasMerge.cpp)须整体联动。

### 更换账号、盘或根目录

每个 destination 只支持一个当前目标，不添加多账号 target_id 体系。`resource_destination_states.target_revision` 持久化推进，用于跨重启拒绝旧目标 worker；云端指纹由核实的账号/盘身份和规范根派生，本地指纹标识实际存储根及配置修订，均不含密钥，不能仅用 remote 别名或相同相对路径代替。

切换时暂停该平台新认领，取消/收尾在途操作，记录旧配置证据，推进目标修订并使该平台旧回执及历史 skip 对新目标失效，然后 dry-run 新目标补建。另一平台、本地文件和旧远端对象保持。同平台切换期间不允许两套配置混跑；无法核实目标身份时要求显式维护步骤，不自动沿用旧成功。

新目标的缺失回执先核对再按显式计划补传。改变 adapter 但实际账号/盘/根相同不必重传，仍须证明平台和目标身份一致；改变根就是新目标位置，不能仅凭 checksum 沿用旧路径。

推进 target_revision 使旧回执通过版本比较失去 current，不在一个大事务里全表重置几千万行；新目标任务通过游标补建。取代最后成功回执前保留必要旧目标证据，既不继续把旧目标标为当前，也不因切换删除旧对象。

本地更换存储根同样先暂停 local、使旧 local worker 失效，再按清单复制/核对或明确修复；不能切换根后因同名相对路径存在就把旧回执视为有效。云端任务、已确认对象和 skip 不受本地根变化影响。所有工厂实例持有不可变配置快照，在途请求不得偷偷换用新账号/根。

## 10. 新接口及旧客户端兼容

已新增 `/api/v2/migu/...` 显式文件 ID 处理、操作查询/取消、逐云端回执分页与设置，API/OpenAPI 为 1.14.0，另含 producer 查询/重试和显式历史生成动作。当前实现范围见[API 说明](../api/mg/migu-api.md)；目录页通过 resources/status 一次附加本地/双端事实，保留旧目录结构；下面表格仍包含通用选择器等后续要求，不能将其全部视作已交付。parallel 配置固定 legacy view 后只桥接旧歌曲/封面 process 及其进度/停止/字段读取；process 未固定旧视图时仍拒绝；已知治理入口接入持久准入并保持原语义，未知写入口拒绝。local 不是 cloud_provider。

| 能力 | 拟议约定 |
| --- | --- |
| 处理资源/封面 | `resources/process`、`covers/process` 接受 destinations、操作类型及 idempotency_key；省略目标时使用已启用配置；明确 local_status 或 provider/cloud_status 筛选，不依赖本地状态选择云端 |
| 派生产物 | 通过受控 `artifacts/process` 处理已登记产物或生成计划，不接收任意文件路径；来源/处理版本校验后调度 |
| 查询回执 | `cloud/receipts` 按 owner_type、resource_id、provider、任务状态及核验级别分页；目录页批量附加 `cloud_receipts`，不逐条查询 |
| 结果字段 | 返回各端任务状态、内容代次、目标修订、current、matches_local、verification、transfer_skipped、assumed_stored 及脱敏结果码；另给 all_requested_targets_current，操作已终止不等于所有副本已核验；云端路径不是公开 URL |
| 批次 | 返回真实父 batch_id，各 item 带 destination/provider；按目标分别计数并另给 logical_resource_count，不把三项数值套入旧 total；请求目标均终止后父批次才终止，明确部分完成、全 skip 和 busy |
| 取消/暂停 | `tasks/stop` 可限定 batch_id 与 destinations；不传 destinations 取消整批；目标暂停/恢复单独操作，不能用父批次一个停止标记替代 |
| 历史接管/补建 | 受控维护 CLI 或管理通道提交已固定 manifest，支持 preview/start/pause/resume/status；普通 process 不接受任意“已上传”声明 |
| 策略 | 新 settings 将各路 mutable 策略与 readonly 目标状态分开；凭据、环境文件和磁盘绝对路径不对外返回 |

### 旧客户端的完成范围

旧字段、默认值、错误及完成时点按入口分别固定，不能用“兼容平台”把原本不同的行为强行统一：

| 入口 | 旧视图的完成范围 | 新增目标如何显示 |
| --- | --- | --- |
| 歌曲 filesystem process | 本地下载及原有后处理语义；process 中原来为空的 cloud 字段继续按原契约输出 | 显式 parallel 配置启动的额外云端任务只在新版视图表达，不拖延旧同步/进度完成 |
| 封面 process | 本地，以及旧配置/固定兼容档案原来要求的单个云端；原来关闭云端时只看本地 | 另一平台结果只进入真实父操作，不覆盖旧成功或延长旧视图 |
| 旧 rclone copyurl | 只由 legacy 模式继续保持原对象路径/语义 | 不能在 parallel 中把远端对象伪装为 local；该客户端应迁新版，未支持时明确不可用 |
| 新版 process | 本次请求全部 destinations | 逐目标状态及真实父操作终态 |

parallel 下为旧入口建立持久化的兼容批次投影：旧接口返回的 batch_id 指向旧范围，另有真实父 batch_id 跟踪全部任务，两者有持久关联并共享实际执行结果，不重复传输。旧进度/列表/items 只显示原逻辑资源视图，不能把真实父行再显示成第二个旧批次；新版可从兼容 ID 解析真实父操作及关联 ID。

兼容视图仅在原范围终止时结束，不等待额外平台；真实父操作继续等待其全部目标。旧同步响应只等待旧范围，不把后台受理当作完成；后台真实父 ID 可通过响应头提供，原 JSON 结构不因此改写。兼容范围超时/失败不回写另一平台状态；旧视图已终止后，迟到云端成功不能将其重新标为运行中。

### 字段、控制和参数兼容

- 保留旧路由、参数范围、HTTP 状态、已有字段类型和空值规则；backend 仍表示实际上传工具，新 provider 才表示平台。旧投影读取回执的 adapter 来源证据，不能用工厂当前配置反推历史上传工具。
- LEGACY_VIEW_PROVIDER 固定旧单回执的查询投影，不按谁最后完成变化；启用旧兼容视图前固定其档案，明确选一个平台或保持原本的 local-only。并行两端但未固定旧云端语义时，不静默挑第一端，不宣称该视图兼容就绪。
- 查询投影与执行开关分开：停用某端仍可读取其历史回执，不切换成另一端。旧写契约要求但当前未启用的目标应明确不可用，不能悄悄将请求降成 local-only；各端事实的 current 也不等同于远端实时在线。
- legacy 模式保留原单端执行且不自动启用歌曲上传。parallel 是显式新配置；旧调用触发的额外目标不能改变上表旧视图。模式选择是业务执行规则，不是选择“旧 SQL”——删列后兼容视图也必须从新表读取。
- 新 cloud_status 筛选必须指定 provider；旧 cloud_status 只作用于固定兼容平台。旧 status、选取范围和分页保持旧语义，不因新平台缺失扩大一次旧请求；需要独立补云端时使用新版目标查询/补建入口。
- 活跃旧批次的整批 stop 解析到真实父操作，取消该操作实际拥有的全部目标，保留原 200 受理语义；未知 ID 404、兼容批次已终止不可取消时 409，按[现有 controller](../../src/api/controllers/CrawlerController.cpp)保留。兼容视图已结束而真实父仍运行时，剩余目标经新版父 ID 单独取消，不能改变旧终态取消响应。
- 旧 stop 可能立即投影 Cancelled，维持旧响应/进度语义；新版仍报告 cancelling/未决尝试直到各路收尾或转 uncertain，不能把兼容状态当作已排空。重启后的 ID 关联、取消所有权和视图终态都必须可恢复。
- 旧 refresh=true 保留旧本地刷新及原兼容上传范围；内容变化可使其他端 current 失效，但不因此扩大本次上传目标。新版明确区分 refresh_content、retry 和 repair_destination。
- 历史声明 skip 不能投影为新核验成功；旧云端不足证据时为待核对，路径按原空值类型返回。旧 process 命中历史 skip 时返回明确的 skipped/result_code，并保留原本地状态，不伪造新下载；旧字段不足以表达的细节由新版查询提供。
- 新写入口在 schema 不兼容、基线未就绪或维护闸门关闭时返回明确的不可用/维护错误，不返回空成功。新旧读取可在维护期按其支持的 schema 继续工作；旧 API 的数据库删列不与对外字段删除绑定。

仅在客户端迁移、两种批次视图和兼容回归完成后再讨论移除旧字段；不能只比较 JSON 字段集合就宣布接口兼容。

## 11. 迁移顺序、部署与回退

原44份已执行 migration 保持字节与 checksum 不变；新增 V2913 是独立物理收缩，不修改旧版本。下列为跨部署阶段的顺序；当前开发库已完成扩展及历史补建，本次只做克隆验证后的 V2913 清理，不以启动服务或完成未来旧协议兼容窗口为前提。具体停写、归档、分表 DDL 恢复及验证见[物理收缩说明](../operations/resource-schema-retirement.md)。

1. **冻结契约与独立演练**：完成四类资源身份、统一工厂、状态/兼容批次、DDL 和 manifest 规则；先验证复合迁移语句分隔、触发器权限及全部约束。只读建立 crawler 初步清单和独立克隆，演练导入/回退，不先修改源库。
2. **停止旧写入并备份**：生产维护时停止调用方、全部旧进程/后台 worker 和会修改资源的采集/后处理，核实无其他实例继续写；保存数据库、文件和未决 journal。只有此时才能固定生产最终快照、身份指纹和高水位，不把较早的演练清单直接当作静止生产事实。
3. **扩展并先部署桥接版本**：新增云端回执、三目标状态、补建 checkpoint、审计、内容代次、约束/触发器及批次关联；保留旧云端列。桥接版本先启动在持久维护闸门下，拒绝所有资源写入和自动调度，允许受控维护及只读核对。先确认没有旧 writer，再做正式导入。
4. **正式导入并封存基线**：使用最终 manifest 分批插入历史双端 skip、本地 skip 和旧值证据，重读并处理变化项。该阶段不自动执行 stale 恢复后传输、补建 worker 或缺回执上传；重跑应幂等且内容 I/O 为 0。计数/引用、manifest 摘要和未决项核对完成后，将 baseline_ready 持久化。
5. **影子读取与小范围放行**：桥接版本验证新旧投影、兼容批次和取消，再只开放明确的试运行资源范围；基线以外的未核对旧资源保持 blocked，不因没有 skip 行而自动处理。只有 schema、目标身份、baseline_ready、兼容档案及 dispatch 闸门全部满足才放行。历史范围仍 skip，源站/网盘测试只用已授权范围。
6. **切换新表读写与受控运行**：先确认所有写入来自支持新回执的版本；当前 V2913 包已不双写旧物理列，兼容结果从新回执投影。是否解除维护、开启统一工厂三路和处理新增范围属于独立运行选择，不是物理清理动作，也不能把历史 skip 写成新成功。
7. **独立物理收缩**：新增 V2913 封存旧云值后删31列/2索引，生产 SQL、触发器、绑定、快照和 schema 检查同步；先在同备份克隆核对本地/业务字段、回执/skip/审计及旧接口，再执行实际库。旧 HTTP JSON 和 `resource_legacy_profiles/batches/operations` 保留，全部协议退役另按独立门禁推进。
8. **验收及保留回退材料**：记录阶段/代码/迁移版本、基线封存标识、内容 I/O 计数、未核验/blocked 项及证据；保留文件、journal、旧值审计和目标修订，核对各阶段备份恢复及旧视图期限。

扩展/桥接包与当前 V2913 收缩包使用各自验证过的 SQL 目录和 schema 支持范围，不能将当前 SQL 交给仍访问旧列的二进制。当前包的 schema_status 要求 V2913；已有库通过独立 schema CLI 完成升级及核对后再选择是否运行服务。DDL 中断重试、迁移锁和 checksum 按[数据库迁移](../operations/resource-database-migration.md)与[服务恢复指南](../operations/service-lifecycle.md)处理。

schema 兼容、基线封存、各目标 dispatch_state 和旧视图就绪是不同闸门，存于持久控制状态并在接收/认领时核对；仅将上传开关设 false 不足以阻止本地重下载或启动恢复。新 worker 在 baseline_ready 前、未纳入清单的旧数据区间及维护期不得开始内容 I/O。恢复服务只触发状态核对，不把历史未知状态自动变成上传候选。

扩展阶段所需字段和桥接流程先于正式回填；收缩阶段不得早于切换验证。当前按可审阅的维护窗口设计，不承诺无停机升级；需要缩短大库窗口时另行验证受控增量捕获，不能允许完全旧 writer 在新版本记录代次期间继续混写。

回退边界：

- 删列前优先回退到支持新表、历史 skip 和维护闸门的桥接版本；关闭新目标只停止调度，不删除回执或远端对象。
- 完全旧二进制不识别历史 skip、内容代次和新维护闸门，不作为恢复自动生产调度的常规回退目标。确需使用时先停止全部新写入和外部调度、验证其 schema 支持并恢复固定兼容投影；只能在封闭维护范围运行，不能声称仍保证历史零重复传输。另一端事实仍保留新表，恢复生产优先使用桥接版本。
- 删列后旧二进制不再兼容，需使用前向修复或恢复匹配版本的完整备份；不能只替换 exe、修改历史迁移或清空 schema_migrations。恢复演练先在独立库和独立文件根完成。
- 远端已发生的上传不可由 DB 回退撤销；通过目标修订、对象核对和幂等复用处理，不在回退时自动删除网盘文件。

## 12. 验证要求与完成标准

按[验证要求](../agents/validation.md)区分离线、本机 I/O、独立 MySQL 和真实平台。以下是完整方案的验收矩阵，基础迁移和导入的实际结果另见[当前交接](../agents/current.md)；尚未接入的运行时场景仍待执行，不能用基础测试通过替代三路、旧批次投影或真实云端验收。

### 基础阶段实际验收（2026-10-29）

代码为 main `83e72e9` 上的 `codex/resource-cloud-receipts` 未提交修改，构建及证据分别位于 `D:/codex-builds/crawler-cloud-receipts-20261029`、`D:/codex-runs/crawler-cloud-receipts-20261029`；未改生产 `crawler` 库。

- `demo`、维护 CLI 及受影响 target 构建成功；`SqlMigrationStatementsTest`、`ResourceTransferPolicyTest`、`ResourceCloudImportManifestTest` 和三项 API 测试共六项通过，见 `offline-final.log`。
- `MySqlResourceCloudSchemaTest` 的全新模式和 `--upgrade-from-legacy` 模式分别通过。后者库为 `crawler_ownership_test_receipts_upgrade_20261029_134207_1f67fb`，验证 V2901 的四类旧回执经正式 runner 升级后保留、默认关闭、非法身份拒绝、触发器缺失可检出、重跑及级联。
- `MySqlLegacyResourceImportTest` 在 `crawler_ownership_test_receipt_import_20261029_134530_828380` 通过，覆盖四类 owner、双端及本地 skip、旧单值 backend、强证据保留、空路径及 adapter 归属、快照冲突、游标/审计原子性、旧页重放、SQL 故障回滚和目标登记竞态。没有内容端口，也没有真实文件或网盘访问。
- 旧 `MySqlMetadataCoverQueueTest`、`MySqlMetadataCoverCloudTest`、`MySqlSingerProfileRefreshTest` 三项回归通过；`MySqlMetadataOwnershipTest --check-sql-only` 在新独立库 prepare 210 份运行期 SQL 通过，没有据此宣称完整 ownership 用例运行。
- CLI 的逐条预览、幂等目标登记、apply、重复 apply 和 status 实际命令通过，见 `cli-fixture-20261029134639.jsonl.results`；摘要错误、缺少双端声明先于数据库连接拒绝，内容请求/上传为 0，baseline 未激活。七个新增生产实现的 clangd 检查退出 0；静态检查见 `foundation-static-check.json`。

上述是基础阶段的历史结果；随后本轮桥接接线的验证如下，不能混为第一阶段已经覆盖。

### 桥接准入与目标装配实际验收（2026-10-29）

- `bridge-offline-final.log`：11 项离线/本机/接口回归通过，包括新增 ResourceTransferAdmissionTest、歌曲同步/异步、两类封面流程、文件恢复和 CloudFileStoresTest；补充工厂本地延迟根的流式提交/清理用例后，CloudFileStoresTest 再次通过，见 `bridge-local-factory-final.log`。
- `crawler_ownership_test_receipt_import_20261029_143424_bc4591`：历史导入测试增加四类实时准入、skip 不依赖文件/在线目标、普通重置后仍不认领、正式 SQL 认领闸门、未激活时不创建关联资源，以及 local/全局维护区分；全部通过。`crawler_ownership_test_receipt_sql_20261029_143451_420714` prepare 216 份运行期 SQL 通过。
- `bridge-http-20261029143517.json`：真实 demo 在独立 fixture 库、关闭云上传及未创建的正式文件根上运行；选定写入口返回标准 503，schema/preview 为 200、未知 stop 为 404，正式根始终未创建。该只读空闲进程测试不作为真实云端、网络抓包或全服务优雅排空的验收。
- 工厂的屏障测试证明百度适配器阻塞时 Google 适配器可完成，以及一端配置错误不丢弃其他目标；这是给定快照的独立适配器调用，尚未证明三路同批登记、独立取得来源、各端预算或重启恢复。
- 关键实现 clangd、编码/格式/链接和两份维护错误契约检查分别见 `bridge-clangd-*.log`、`bridge-static-check.json`。静态文档的 22 个受保护写操作均有 503，并按四类补池展开核对运行期响应；SchemaStatus 的 12 个属性及原错误封装保持一致。

下表是部署验收要求；三路服务/统一任务接线、基线封存和旧批次投影已实现并有隔离验证，真实三路上传、生产迁移和规模性能仍未完成。库内任务和版本确认的验证不等同于正式运行验收。当前 bridge 准入只阻止新操作/认领，不能替代对已接收任务、旧 writer 和其他进程的停写、排空及版本围栏。

### 完整方案验收矩阵

| 场景 | 必须看到的结果 |
| --- | --- |
| 三路启动 | 用屏障/事件证明本地尚未完成时两端已能执行；任一路被阻塞时另两路可独立终止，不能只比较总耗时 |
| 工厂与本地链路 | 同一工厂覆盖 local/baidu/gdrive；local 禁用/根不可写时云端不打开正式根；流式 write、完整 commit、条件确认、accept/defer/abort、旧文件及 journal 恢复均验证 |
| 组合与配置 | 仅本地、仅任一云端、双云无正式本地、完整三路、全部关闭、单端配置错误及新旧配置优先级均符合规则 |
| 失败/取消 | 每路失败、超时、取消、暂停不改变其他路成功；全批取消覆盖全部；队列中和在途两种情况均有检查 |
| 并发和资源界限 | 每端与全局并发/限速有效；慢端不占满其他端配额；大音频内存有界，临时区满只影响对应路 |
| 版本和竞态 | 三路不同字节报告冲突；本地迟到不重置云端；旧 worker 的远端实际写入也不能覆盖新版本；目标切换和 DB 响应丢失均拒绝误确认 |
| 历史双端接管 | crawler 脱敏样本不论旧 baidupcs/rclone 均产生两端声明 skip 及本地策略；重启后普通 process、probe 失败及策略升级仍不读本地内容/请求源站/下载远端/上传，相关调用计数全为 0 |
| 幂等和中断 | 同 key 重试沿用冻结资源/配置，不因新数据扩大；不同操作 busy 不接管，取消不误伤；manifest 重跑不降级，逐 ID 冲突与 cursor 原子恢复，新增/刷新不漏不重 |
| 校验兼容 | SHA-256、带算法 hash、MD5、size-only、缺值、无本地文件、元信息失败和内容冲突的等级/模式不混淆 |
| 新平台和新目标 | 先预览和补建，已存在对象可复用；未知不按缺失上传；旧 target_revision 的 skip/worker 不作用于新目标 |
| 派生产物 | local 关闭时 producer 仍可为两端供给快照；两种产物同版本、来源/处理版本变化、旧路径工具边界、历史 skip 不触发生成及钩子幂等全部验证 |
| 删除和归一 | 四类真实外键、BEFORE 身份校验触发器、非法直接写入/upsert、父表级联、别名双端冲突及 journal 引用保护全部验证 |
| API 兼容 | 旧歌曲/封面分别验证完成范围；额外端阻塞时旧视图可终止、真实父继续；两种 ID 重启可恢复、旧 stop 的 200/404/409 及新版定向取消均验证 |
| 生命周期 | 兼容视图/工厂对象释放时其他目标继续安全执行；单端预算不触发父级超时取消；关闭顺序、宽限到期和未决日志核对有证据 |
| 升级/回退 | 全新/旧/含未识别库；分句器完整执行复合触发器、权限不足、DDL 中断/重跑；先桥接后导入、baseline 闸门、旧 writer 拒绝和收缩后 legacy 视图分别演练 |
| 真实双端 | 用户授权的独立范围核对对象、目标身份、大小/hash、DB 回执、重复任务和单端取消；不以替身冒充上传验收 |

已有相关 target 包括 `MetadataCoverDownloadWorkflowTest`、`MetadataCoverCloudWorkflowTest`、`CloudFileStoresTest`、`ProcessRunnerTest`、`MySqlMetadataCoverQueueTest`、`MySqlMetadataCoverCloudTest`、`MySqlMetadataOwnershipTest`、`SongResourceDownloadWorkflowTest`、`AsyncSongResourceDownloadWorkflowTest`、`LyricProcessingWorkflowTest`、`LocalLyricArtifactStoreTest` 和三项 API 测试；本地流式/文件改动另选 `AsyncHttpStreamTest`、`AsyncFileBlobStoreTest`、`SongResourceDownloadStorageTest`、`ResourcePersistenceRecoveryTest`、`MetadataCoverStoreTest` 等实际受影响项。实际注册以 [CMake](../../CMakeLists.txt) 为准；新增工厂/三路调度、迁移分句/身份触发器、历史导入/续跑和 artifacts 云端场景需扩展有意义的覆盖并登记，不能把未来测试名写成已有 target。

性能验收至少覆盖千万级文件资源、两千万级双端回执；资源数包含 artifacts，跳过/成功占绝大多数、少量 pending/retry，并发刷新及历史补建同时出现。记录 p50/p95/p99、扫描行数、锁等待、冷/热缓存、内存/临时盘、源站请求数和索引/数据体积，另计四个资源外键索引及触发器开销；用实际查询计划验证索引。唯一键、provider 队列索引、目标修订无全表更新和资源页批量查回执必须实测。

全量精确计数、清单对账和证据升级不承诺即时完成。两端当前回执行通常至多为纳入范围资源数的两倍，任务/事件/迁移审计另计；旧远端对象和历史版本文件也另计。仅在实际查询压测证明需要时再加入可重建摘要缓存，不提前持久化两个 stored 布尔值。

## 13. 收益、成本与后续实施入口

- 三路独立调度提高某一目的地异常时的可用性，工厂隔离平台选择，回执按目标保存；遵循 SRP、DIP 和 DRY，不把双端判断散落到每个 workflow。
- 历史接管通过有范围的声明 skip 避免重复传输，并保留核验事实；新平台通过有游标的补建逐步接入，不以全库 reset 代替迁移。
- 成本包括独立获取可能增加的源站读取、临时空间、回执/索引存储，以及并发状态与兼容维护；不宣称独立三路必然比串行更省带宽或已达到大规模性能目标。
- 当前每平台只维护一个目标，复用已有批次和 adapter；不提前加入通用插件框架、多账号调度或跨所有者内容寻址系统，保持 KISS/YAGNI。

开发顺序为：确定 DDL/状态/兼容批次 → 验证迁移分句与约束 → 独立库演练历史导入 → 实现包含本地下载的统一工厂/三路调度及桥接闸门 → 四类资源及恢复 → 基线封存和新旧 API 演练 → 双端真实及规模验收。先实现独立维护工具用于验证持久语义，不改变生产停写、桥接先于正式导入的要求。生产上线严格按第 11 节的停写、先桥接、导入封存、放行和收缩门槛推进，不能把开发演练顺序当作生产迁移顺序。

索引和现状说明仍从[项目文档入口](../README.md)、[架构](../architecture/README.md)、[资源规则](../migu/resources.md)、[SQL 职责](../../sql/mysql/README.md)、[配置](../configuration.md)和[服务恢复](../operations/service-lifecycle.md)查阅。实施完成后同步这些说明及两份 OpenAPI，现阶段不能把待实施配置复制成现有可用操作。

数据库设计参考仍为 [MySQL 联合索引](https://dev.mysql.com/doc/refman/8.0/en/multiple-column-indexes.html)、[执行计划](https://dev.mysql.com/doc/refman/8.0/en/explain.html)和 [InnoDB 计数](https://dev.mysql.com/doc/refman/8.0/en/aggregate-functions.html#function_count)，这些入口不构成本项目迁移或性能验收证据。


2026-10-30 历史发现/队列容量阶段：V2911、固定范围只读stat/meta发现、持久页控制/报告、数据库容量和满队原子回滚已实现。demo/CLI及受影响target构建，5项离线/API、发现/容量两项空隔离库业务测试、V2901升级/重复迁移、370份SQL prepare、实际隔离CLI和7项clangd通过；10个v2路径/21个schema的两份契约保持一致，API1.13.0。真实网盘及生产库未操作，单端报告自动采信和规模部署验收仍不在已完成范围。证据目录D:/codex-runs/crawler-cloud-receipts-20261029的discovery-verification-final.json；操作见[历史发现](../operations/resource-history-discovery.md)与[容量配置](../operations/resource-transfer-tasks.md#持久队列容量)。


2026-10-30 四项收尾开发/准备阶段：单端审阅采信、双端导入控制、基线supplement、旧writer结构化升级指导、清理捕获/预演及上线/收缩工具已交付；V2912、API1.14.0。主agent集成3个同模型子agent，6项离线/API、16项工具自测、7项隔离库验证（含382份SQL prepare）、实际CLI闭环及8项clangd通过。正式crawler/真实云端/远端删除/收缩DDL均未执行，旧writer未全部迁接；部署条件不能用准备自测代替。证据D:/codex-runs/crawler-cloud-receipts-20261029/closure-verification-final.json，固定四项状态见[当前交接](../agents/current.md)。

2026-10-30 真实库克隆/恢复与进程预演阶段：主 agent 单独执行，正式 crawler 只读、前后仍 V2026102601。实际旧库快照在独立 MySQL 的白名单克隆库升级至 V2912，4,439,979 行原业务数据前后一致，44 条 migration checksum 及 382 份运行 SQL prepare 通过；旧 cloud_* 列保留，移除列仅属既有 V2026102801。另一 UUID 实例同名空库恢复后完整数据摘要及触发器定义一致；工具显式分离 backup.sql/triggers.sql、先数据后触发器、按表事务恢复，并拒绝未支持的 scheduled events。两真实仓储探针进程被强杀后，2 个未决对象/槽保留、显式修复完成、旧认领回写拒绝且文件未覆盖；它是本地文件/数据库 fixture，不代表完整服务 worker 或网盘验收。新增探针 configure/build、clangd 及 22 项工具自测通过。旧资源中 45,558 条有 baidupcs/双路径，其中 5,699 条为 rclone:size，不能自动扩大双端历史跳过或提升强证据。阶段记录 release_scope_bound=false/release_authorized=false；真实三路、完整文件/journal、规模/调用方及正式切换仍按原四项收尾条件推进，用户正在登录两端。本轮两台临时 MySQL 已正常关闭，生产服务和数据未改写；证据 D:/codex-runs/crawler-cloud-receipts-20261029/acceptance-20261030/acceptance-verification-final.json，详细范围见[上线说明](../operations/resource-release-readiness.md)与[当前交接](../agents/current.md)。
2026-10-30 同模型并行真实验收阶段：主agent先从当前turn_context核实gpt-6.1-sol/xhigh，显式同参新建cloud_live_v2/service_recovery_v2/file_journal_v2，执行前后实际metadata一致。补原生Google Drive交互OAuth账号/实际目录核验，复用既有rclone登录与代理、强制官方HTTPS/TLS/认证隔离，三项针对性离线回归及构建通过。两个真实demo本地worker强杀/真实过期恢复/HTTP repair2成功，旧认领拒绝、SHA/mtime不变、无重传，恢复4.828秒。用户开启百度WebDAV写权限后，真实工厂三路并行与分别取消两端共3轮全过；双端新独占前缀各2个完整512KiB对象、完整SHA回读匹配，取消0.750ms/31.511ms、兄弟路独立完成。RC正文起点早于local完成，只将其称为RC数据事件；云完成先后不作并行成立条件。新增服务/文件/云端探针和驱动，四owner及停止服务fixture的文件/journal备份还原作为开发回归保留。用户明确历史本地无需验收、原路径保留，后续不再追查或据缺失触发补下载。全部临时服务关闭，原配置未改，生产写入/远端删除/音乐源请求0；API1.14.0/schemaV2912不变。证据D:/codex-runs/crawler-cloud-receipts-20261029/parallel-verified-20261030-122437/parallel-verification-final.json，release_scope_bound=false；规模、部署调用方/生产窗口及原兼容期收缩仍按固定四项收尾，不把本轮分层验收当已正式切换。
2026-10-30 规模与部署准备收尾：主agent及新scale_readiness_v3/deployment_readiness_v3实际同为gpt-6.1-sol/xhigh，两agent已正常结束。补显式历史本地免验scope及数据库路径摘要工具，旧计划摘要兼容；45,558条路径最终一致，不读旧文件。新增双认证RC/三路配置、调用方声明/停写/capture部署检查，63项工具自测及探针构建通过；外部writer仍unknown，未正式切换。4439979行背景库的210次真实受理/6300任务全部完成，但并发4锁获取总耗时p95=866.805ms超过测前500ms，首测失败保留；Windows目录缓存统计已仅在探针修正，本机12MiB/结束0及容量4争1、幂等和多端原子拒绝补测通过，不能据此改判原规模或回填其RSS。测试库已正常停机、原服务未动，无DDL/生产写入/云写/音乐源请求。仍属原第3项工程目标与第2项实际部署收口，未新增另一组功能项；详细指标、边界和后续见[上线说明](../operations/resource-release-readiness.md)、[生产部署](../operations/resource-production-deployment.md)与[当前交接](../agents/current.md)。证据readiness-20261030-131531/readiness-verification-final.json。

2026-10-30 并发4与完整规模测量闭环：同模型3子agent并行完成入队实现、工具及独立语义审查，主agent统一隔离副本/构建/回归/独占测量。请求内prepared复用、当前锁定容量标量计数后，进一步仅为Process复用原规划并以三条顺序批量SQL持久化；其他动作逐项写入，pipeline仅用于必执行的读对。保留原全部负载与阈值，最终210样本/6300工作任务非预期失败0；并发4锁上界p95从原866.805降至426.890ms，enqueue p95=530.679ms、混合受理4.407请求/s。native同轮RSS峰17575936字节、真实staging峰12582912字节/2494周期扫描/结束0；容量争抢、重放及整笔回滚通过。最终四项MySQL回归、23项工具自测、构建/clangd通过，含RR旧快照与真实持锁、大整数及后段故障原子回滚。中间527/748ms失败轮全部保留，无测后放宽；旧45558路径摘要不变，测试实例正常关闭，原服务/生产未改，schema/API不变。原第3项的本轮工程性能缺口已关闭，正式联合发布范围、原第2项实际调用方与生产停写窗口仍待处理，原第1/4生产采信和兼容期收缩不自动放行。详见[上线复测](../operations/resource-release-readiness.md)、[当前交接](../agents/current.md)与证据lock-scale-20261030-134522/lock-scale-verification-final.json。
