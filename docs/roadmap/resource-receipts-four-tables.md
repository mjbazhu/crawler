# 四类云回执拆分与资源运行精简方案

更新日期：2026-10-30。状态：讨论方案，尚未修改代码或执行DDL。源码检查点`6d59b66`，实际crawler为V2026102913。本方案是后续实施依据；[原方案](resource-cloud-receipts.md)保留V2913实现与验收历史，原历史治理/兼容要求不自动延续。

## 1. 已确认的范围

- 只按资源归属拆四张云回执表。下载、上传、MRC生成和关联资源仍统一编排，任务表不随资源类型或网盘继续拆分。
- 保留MRC派生生成、音频关联资源下载及其运行表。保留三路独立、逐端配置/并发/取消/重试、持久恢复、目标版本和校验围栏，复用现有实现。
- 退役本轮新增的历史治理和旧接口兼容体系，不再要求全库preview/apply/review/seal/activate才能启动正常业务。
- 原crawl_batches/items/events旧任务记录暂留，不迁移、不重放，不据旧任务终态生成新回执。歌曲、歌手、专辑等原业务数据及本地路径保留。
- 本次先完善方案；不启动服务、不激活基线、不请求源站或网盘，不重跑既有全量实测。以后实际改代码/DDL时只验证受影响行为与数据保全。

## 2. 四表命名、关联与原位改名

| 表名 | 直接关联 | 当前数据处理 |
| --- | --- | --- |
| song_resource_cloud_receipts | song_resources.id | resource_cloud_receipts原位更名，保留全部91,116行及原主键 |
| singer_resource_cloud_receipts | singer_resources.id | 新建空表 |
| album_resource_cloud_receipts | album_resources.id | 新建空表 |
| song_resource_artifact_cloud_receipts | song_resource_artifacts.id | 新建空表；每个具体产物有自己的回执 |

四表均用resource_id作为本表资源外键，cloud_provider继续使用baidu/gdrive；主键id和唯一键(resource_id,cloud_provider)保证每资源每端一条。四张表各有一条真实FK及ON DELETE CASCADE，不再使用owner_type和四个可空外键表达归属。artifact表名沿用song_resource_artifacts的归属前缀。

2026-10-30只读核对：91,116行全部owner_type=song，resource_id=song_resource_id；其他三类外键均NULL；百度/Google各45,558行。没有其他表通过数据库FK引用回执id。因此采用RENAME TABLE配合约束调整，无需INSERT SELECT、导出重灌、重新分配id或逐行迁移。

歌曲表保留id、resource_id及所有有效云事实/策略/版本/上传意图。删除owner_type、song_resource_id、singer_resource_id、album_resource_id、artifact_resource_id这些重复归属列；将唯一键和队列索引去掉owner_type，增加resource_id到song_resources的直接FK。删除两条多态归属触发器，不能留引用已删列的触发器；移除原baseline检索索引，策略来源字段不再为治理查询建索引。新建三表使用同一份字段规则与各自真实FK，公共处理代码复用。

只有实测前置条件成立才走更名：仍全部为song、归属匹配、无孤儿和在途任务；若期间出现其他owner数据，明确停止迁移并修订方案，不静默丢弃。更名保留原Google未解析路径，不改网盘对象名或移动文件。

**更名免去的是数据搬运，不是所有代码修改。** 所有运行读写仍须改到四表，SQL参数去掉冗余owner/外键绑定，schema检查、查询分页、状态映射和删除/合并同时调整。用ResourceOwner枚举映射固定SQL目录，不接受客户端拼表名；继续共用执行逻辑，不复制四套下载/上传流程，也不保留旧表名视图或双写桥接。

四表使用各自独立的约束名称，显式建立正确父表FK、provider限制、唯一键和索引；不能把CREATE TABLE LIKE视为已复制外键/触发器，也不能复用歌曲表的父关联。迁移前重新核对所有入向FK和触发器，不依赖本次观察的“无入向FK”长期不变；遇到未知引用先停止，不使用FOREIGN_KEY_CHECKS=0强行删列。

回执id只在所属表内唯一，跨类型查询必须携带owner。现有cloud/receipts已要求owner，继续保持；不能新增只凭裸receipt id跨表更新的接口。本地回执继续留在四资源表，不塞入云表。

### 2.1 统一执行中必须同步调整的读写

| 现有入口 | 四表后的要求 |
| --- | --- |
| [PreparedQueries](../../src/infrastructure/music/persistence/mysql/MySqlResourceTransferPreparedQueries.h) | 在目标热点锁前按本次owner准备对应表的lock/ensure模板；删除baseline_blocks预备语句，缓存key继续包含完整SQL路径，不能只用相同basename混用四表statement |
| [批量Process登记](../../src/infrastructure/music/persistence/mysql/MySqlResourceTransferBatch.cpp)及ensure_process_batch | 仍先批量插入共享tasks/slots，再按owner过滤、至多对4张云表各ensure一次；全部在同一事务/同一连接，顺序await。不为local/skipped/busy创建假云任务，不推算跨表自增id，后段任何失败整组回滚 |
| 单项Process/Retry/Repair、beginAttempt/claim/complete/finish | 每条路径都走同一固定owner到SQL目录映射，保留内容/来源/目标/认领/取消检查；不能只改成功回写而留下失败、修复或刷新访问旧表 |
| [批量状态](../../sql/mysql/resource_cloud_receipts/status_batch.sql)及回执分页 | 用固定四表JOIN或UNION ALL投影统一列，按owner+resource_id精确关联；保留输入顺序、同数字不同owner、缺失项、路径未解析及弱校验语义。分页cursor仍限单owner；不逐资源发查询或探测网盘 |
| 目录页、本地预检、目标切换、别名与删除guard | 同步更新四表引用、policy_origin、行映射和参数顺序，不能只改resource_cloud_receipts目录。四资源表transfer_admission/snapshot等动态路径也须核对 |

混合owner操作仍共用一条父操作和统一任务表。锁顺序保持父操作树→排序的owner/resource→目标→任务/回执；多个回执表按确定顺序处理。保留请求内prepare缓存和有界批量SQL，不能退回600项逐条ensure导致热点锁往返膨胀，也不能把多条DML并行流水线发送。新增最多4次ensure是有界变化，旧性能数值不自动代表新SQL的性能；仅在锁范围/批量策略改变或出现退化时补相应测量。

所有四表模板输出相同类型和列顺序，明确CAST无符号计数/代次/大小，避免UNION/COALESCE与字面量0将值推导为DECIMAL后被uint64At读成0。ResourceOwner、task/slot的owner_type、云对象路径中的owner以及产物source_resource_id继续保留；仅删除回执表的多态归属列，禁止全仓替换owner_type。

## 3. 其余16张表的逐项决定

当前运行表都为空，它们承载今后的真实任务，不是旧crawl任务的迁移结果。历史资料已经转化为当前skip/回执后，迁移过程表退出常驻运行。

| 表 | 实际职责 | 本方案处理 |
| --- | --- | --- |
| resource_destination_states | 三端身份/修订、维护与队列容量 | 保留并瘦身；移除baseline_ready/manifest及基线依赖 |
| resource_transfer_operations | 一次新请求的幂等、范围和父操作 | 保留；不导入旧批次、不保留旧单端视图快照 |
| resource_transfer_tasks | 各资源各目标的真实执行、重试、取消和恢复 | 保留一张共用表，继续区分owner/id/destination |
| resource_transfer_slots | 同资源同目标互斥，保护未决写入 | 保留；不能用旧任务是否completed替代 |
| resource_producer_tasks | MRC生成预算、认领及派生产物子操作 | 保留，用户已明确需要；独立于历史补建 |
| resource_transfer_related | 音频确认后关联资源的幂等和子操作 | 保留，用户已明确需要；独立于旧批次 |
| resource_cloud_backfills | 历史清单导入的8个逐端run/游标 | 一次性归档后删除，正常运行不查run |
| resource_cloud_migration_audit | 1,505,955条历史判定及45,558条删列原值 | 一次性完整归档后删除，保留当前回执/策略事实 |
| resource_cloud_baselines | 全范围封存与激活 | 为空，解除引用后删除，移除基线CLI |
| resource_baseline_blocks | 迁移排除和别名阻断 | 为空，解除引用后删除，不保留全历史范围闸门 |
| resource_discovery_runs | 历史网盘发现任务/租约 | 为空，移除常驻发现系统后删除 |
| resource_discovery_observations | 历史发现的逐对象观察 | 为空，随发现系统删除 |
| resource_cloud_cleanup_candidates | 删除前云证据与后续清理预演 | 为空，移除旧capture/preview体系后删除；本轮不实现远端删除 |
| resource_legacy_profiles | 旧客户端固定单云视图 | 为空，删除旧视图配置/查询及此表 |
| resource_legacy_batches | 旧进度和完成范围投影 | 为空，移除投影observer后删除 |
| resource_legacy_operations | 旧批次到新操作的映射 | 为空，移除映射/旧并发认领闸门后删除 |

结论：保留6张运行表，归档并退役10张治理/兼容表。加上原表更名及3张新回执表，预期39张基础表变为32张；不把原有21张业务/元数据表算作本次新增。表少并非唯一目标，不能把已验证的并发取消恢复另改成一套内存任务。

历史原因字符串和evidence里已有legacy/baseline字样可作为不透明事实保留，不为追求字样清零改写证据或解除skip；清理对象是执行分支和在线依赖。

运行表仅删除legacy/baseline专用字段或JSON分支。保持操作幂等、父子关系、producer/关联任务、任务预算和claim/intent围栏；本轮不重写调度引擎，也不为了去重状态字段再迁移一遍上传协议。回执当前任务态/未决意图在现有统一执行链中仍有用途，保留到明确证明可由任务表完整替代。

### 3.1 删表前的共享依赖拆除

- [MySqlLegacyResourceTransferClaim](../../src/infrastructure/music/persistence/mysql/MySqlLegacyResourceTransferClaim.cpp)的legacyClaimAllowed仍被正常文件任务认领和producer认领调用。只移除旧批次并发闸门及调用，不删除producer/current、统一槽或任务claim检查；否则三张legacy表删除后，新任务即使没有旧批次也会SQL失败。
- baseline_blocks和baseline_ready不只在维护CLI：还出现在受理规划、PreparedQueries、文件claim/current、producer current、四类transfer_admission、目标查询行映射中。全部改为当前资源策略+本端目标有效性；目标list/lock/transfer_lock的列删除必须与TargetState/reader同步，不能留下固定列序偏移。
- [captureOnConnection](../../src/infrastructure/music/persistence/mysql/MySqlResourceCloudCleanupCapture.cpp)同时承担删除保护。删除历史capture写入前，先把资源/目标锁、槽、活动文件任务、waiting_input producer、running/uncertain回执及attempt检查保留为不写历史的删除guard；四类资源都要检查本表云意图。[父删除](../../src/infrastructure/music/persistence/mysql/MySqlResourceDeletion.cpp)、缺失音质清理和[专辑别名合并](../../src/infrastructure/music/persistence/mysql/MySqlMetadataAlbumAliasMerge.cpp)共同接此保护，不能删掉capture调用就裸DELETE。
- 别名合并去掉alias_transfer_archive/block和旧审计读取，当前album回执查询改到album表；保留双方占用/内容参考/目标修订检查。无法无损合并时整事务拒绝并保留双方资源，而非先删来源再补救；相关路径回收只在提交后按当前引用判断。
- 正式schema_status、最低版本、四表约束、目标字段及SQL模板清单与新DDL一起改。删除已退役SQL文件和旧CMake/CLI/配置注册，不能靠“没有调用”绕过全运行SQL prepare；旧schema/target/queue/recover-tasks命令继续服务当前系统，历史导入/发现/基线/cleanup命令退出。

## 4. 历史数据转为当前事实

- 45,558个历史资源：local及百度/Google跳过继续有效；91,116行verification=operator_attested，confirmed代次仍未伪造成功。旧大小/摘要原样保留，不重读本地文件、不重取歌曲、不重新上传。
- Google的45,558行path_unresolved保持；声明跳过不依赖补造路径。百度旧路径和原工具来源按已有证据保留；rclone仍可能是百度WebDAV或Google。
- 现有baseline_id/local_skip_baseline共有来源标记91,116/45,558条：原值保留，分别改名policy_origin/local_skip_origin，作为普通策略来源字符串，不再关联封存表或控制调度。
- 其余1,460,397条此前只是迁移清单excluded，不代表资源已保存或永久禁止下载。退役迁移排除后，允许显式新请求按当前资源状态/来源限制受理；不自动全库排队，status=4等真实来源限制不因删历史表而清空。
- 原crawl_*当前有484批、124,523明细、525,022事件。保留表和记录，正常元数据业务仍可使用；不将旧批次重放为新三路操作、不扫描它们推导云成功、不因内容不完整主动“修复”任务。
- 一次性归档放在受保护、仓库外的备份目录，记录行数及文件摘要。归档不变成新数据库历史表，也不作为每次启动的在线依赖；完成保全后删除正常代码对旧审计/run的读写。

## 5. 三路和工厂的实际状态与收口

当前[ResourceTransferFactory::build](../../src/infrastructure/app/ResourceTransferFactory.cpp)已经装配local_transfer和百度/Google各自transfer；[组合根](../../src/infrastructure/app/ResourceTransferComposition.cpp)为每端建立独立来源、worker、预算和取消上下文，[ResourceTransferRuntime](../../src/infrastructure/app/ResourceTransferRuntime.cpp)按端启动worker。第一份快照确认参考摘要后其他路比较参考，云端不等待正式local成功。一端装配失败或取消不会取消另一端。原有并发4、RSS/staging及真实云端分层验收证据保留，不能写成本方案修改后的新实测。

核心结构继续使用：资源身份 → 一个统一操作 → local/baidu/gdrive各自任务 → 工厂提供的目标策略 → 本地或对应资源的云回执。MRC和关联子操作仍接同一任务体系，不按每个文件种类再建任务表。

需要清理的是工厂和装配旁的旧分支：legacyProvider/legacyCloud、CloudFileStore单端bytes装配、固定legacy view、legacy/parallel双模式、旧rclone copyurl存储入口、旧本地成功后串行上传流程及旧批次observer。共享的rclone客户端、百度协议、流式上传/摘要/文件安全helper继续复用；不把删除旧业务入口等同于重写底层传输工具。

正常资源执行只走现有统一文件传输port。逐端保留enabled、adapter、RC/remote、concurrency、timeout、retry、限速和staging配置，复用已有登录；百度继续支持rclone/WebDAV，Google继续rclone，BaiduPCS能力在有使用需求时保留为适配器，不成为百度必选。全局CRAWLER_RCLONE_*可继续作为逐端连接的公共默认值，它不构成第二套下载链。仅清理已无入口的单端执行开关/平台推断，不要求复制登录文件、重新OAuth或修改用户rclone.conf。

启用只检查schema、显式维护状态、所选目标身份/修订、相应配置和正常队列状态；不再读历史全量清单、baseline或旧客户端profile。目标切换仍保护本端在途/未决，旧目标事实不能冒充新目标成功。MRC密钥只影响生成计划，不把缺钥匙任务标成功。

移除基线后仍须有最小可用的启停入口：在现有维护CLI中提供目标状态命令，显式选择local/baidu/gdrive、期望目标修订和active/paused/maintenance，短事务更新现有destination行，不新增控制表或审阅run。configure-target只登记目标，不自动激活。三端全部maintenance表示全资源维护；单端maintenance/paused只影响本端，不能沿用当前writes_allowed对“任一端maintenance即全局拒绝”的条件。所选端具备能力时独立受理；单端暂停/不可用不取消兄弟端，状态命令不能直接清空任务/slot/attempt或伪造终态。在途按本端既有取消/预算/未决规则收尾，显式任务取消走同一统一服务。数据库写故障或操作级容量超限仍按既有原子规则拒绝，不混淆执行隔离与事务原子性。

配置校验需要区分：非法全局参数可拒绝启动；单端身份/连接/装配失败关闭该端并保留脱敏原因。现有工厂和逐端执行提供了基础，不能将它概括成任何无效配置都不会影响启动。

### 5.1 MRC、关联下载与恢复的保留契约

1. [producer完成事务](../../src/infrastructure/music/persistence/mysql/MySqlResourceProducerTasksWrite.cpp)仍一次完成：校验producer/父取消及版本→绑定MRC内容参考→登记真实artifact id→受理本端产物子操作→确认producer。任何子受理/四表ensure失败整事务回滚，不拆为“先生成成功、以后再排子任务”；父MRC已终结不妨碍合法producer继续。
2. 音频关联发现、父音频确认及受控related子操作仍同事务；保留root audio→related MRC→artifact两层关系、索引、实际生成的幂等键和同目标取消传播。保留request_snapshot中的adapters、strict_sha256、producers、source_options、process_related_resources、parent_operation_id/parent_task_id/parent_claim_generation及child_kind，不因它们与旧桥接共用helper而删掉。只退役明确的legacy投影字段。
3. 普通历史skip仍不启动来源、producer或关联下载；显式produce_artifacts可以在保留原MRC三端skip的条件下生成派生产物。缺密钥保持waiting_input且不取来源，不伪报完成；恢复、有限重试及producer并发预算保持现有行为。
4. 受理、claim、执行中shouldStop/current、producer current及完成确认均按本端状态判断。兄弟端maintenance/paused不能使本端producer或普通任务失效；来源/内容变化、本端目标修订、认领代次、取消和版权限制仍须拒绝迟到确认。容量超限/数据库错误仍保持父操作级原子性。
5. 保留MySqlProducedArtifactRegistry、解码器、TimedLyricsEncoder、共同enqueueOnConnection/frozenProfile/aggregate/cancel树及文件发布/未决检查。artifact回执关联产物自己的id，不误用父MRC id；任务/producer/related及其父子FK、生成列、唯一索引不在删表范围。
6. 去掉legacy模式条件时，不得顺手无条件启用旧recoverMiguStale或扫描旧批次重放。正常recover只处理当前统一任务/producer；本地journal核对仍保护有效引用和未决写入，不以“不再有旧任务”批量删除文件。旧历史文件免验不取消新任务的文件完整性检查。

## 6. 旧兼容彻底退出的边界

1. 资源处理以现有v2统一服务为唯一写入链路。移除旧process/进度/停止/retry/reset/recover原生资源入口及旧单端DTO/固定平台投影；撤销路由由框架返回404，不再执行旧handler或返回legacy_parallel_unsupported。新版API已提供的能力逐项映射，缺少必要业务能力时补入同一服务，不恢复旧链路。
2. 普通歌曲/歌手/专辑目录、元数据采集/补池、登录态及正常治理业务保留。查询改为本地结果+所属回执表的双端事实，移除单组cloud_*字段；分页保持批量读取，不能每行分别查两次网盘或数据库。新接口明确此次请求/响应有不兼容变化，同步运行期/静态OpenAPI及调用方。
3. 删除ResourceParallelLegacyRouteSink、LegacyResourceTransferService、ResourceLegacyMigrationGuide、legacy claim/projection观察器及旧CLI命令；正常元数据入口的写保护仍须保留，不能删除整个路由层作为清理捷径。混合maintenance/recover-stale去掉旧资源队列恢复部分，正常元数据恢复保留，资源恢复统一走当前recover-tasks。正常元数据维护准入与资源准入分清，云端离线或local关闭不应成为元数据采集的基线阻碍；保留明确人工维护控制。
4. 仓储/SQL全部直接读写四张表；不建resource_cloud_receipts兼容视图、不双写、不保留legacy模式回退。owner枚举仍可用于统一请求、任务和固定SQL路径选择，它不是数据库多态外键的兼容层。
5. 普通父删除/音质占位清理继续按业务语义执行，并保留运行任务/未决对象围栏；云回执按真实FK级联，不再写历史capture表。本方案不自动删除本地历史文件或网盘对象。别名合并不能丢失任一有效确认：无法证明当前事实可合并时返回冲突、保留原记录，不再用历史block表隐式处理。

### 6.1 路由和共用能力按职责拆除

- 保留现有`/api/v2/migu/`资源处理、operation任务查询/取消、回执查询及批量status，接统一服务；同时保留MRC解码/产物登记和关联发现。API中的owner仍用于选择资源类型，不是旧兼容字段。
- 撤销旧资源写路由：`POST /api/migu/resources/process|retry|reset|recover-stale`及`POST /api/migu/covers/process|recover-stale`，不再执行旧原生worker或包装旧成功结构。对应旧资源专用进度/停止投影退出；OPTIONS仍按统一CORS策略处理，其成功不视为旧业务接口仍可用。
- 正常目录、`GET /api/migu/resources/queue`、`GET /api/migu/covers/queue`、只读资源preview及`GET /api/migu/covers/{owner}/{id}/content`可保留URL，统一改为当前事实查询/受控文件读取；不再按legacy profile投影单云。保留本地内容读取、封面URL元数据登记及普通元数据任务的查询/停止，不能把整个MetadataCoverService/CrawlerRoutes或通用任务功能一并删掉。
- 通用crawl_*历史任务查询仍按原记录展示，既不映射成新operation进度，也不通过查询/停止触发旧资源恢复、重放或补传。资源实时进度和取消只管理新统一operation/子任务；元数据任务的既有查询/取消按原业务保留。
- 契约更新同时覆盖DTO、运行期OpenAPI、静态YAML、设置/只读字段、CLI帮助和实际调用方；同一URL保留不代表旧JSON仍兼容。当前资源schema版本及历史记录继续如实标注，此次断约另按新版本发布。

## 7. 实施与迁移顺序

1. 固定当前数据库和源码，停止实际writer、确认维护及无在途/未决；当前服务未运行，不虚构生产观察窗口。核对单song前置条件和现有0运行任务。
2. 在实现分支完成四表SQL路径和统一读写调整，移除上文旧入口/基线/历史依赖；先用克隆环境，保持实际库不变。四表共用同一字段与状态规则，文件移动/重传算法不改。
3. 增加下一组前向迁移：建议V2026103001负责3张空表、歌曲原表更名、归属约束及策略来源字段调整；V2026103002负责删除10张已归档历史/兼容表、目标baseline列及无用标记。最终版本以实施前ledger核对为准，已有45份迁移不改，V2911换行例外继续保留。
4. 在迁移前完成一次性备份/归档并记录摘要；先克隆执行相同迁移和针对性核对，再清理实际crawler。MySQL DDL不保证整文件回滚：以实际表/列/约束前后状态判断可续跑，遇到冲突明确失败；不另造一套长期迁移审计系统。
5. 每个阶段有可识别的前/后置条件：原表与歌曲新表不能同时作为写入源；四表齐备后再收缩重复列；最后确认代码无在线依赖才删治理表。若中断，按前向状态续迁或恢复同版备份，不改旧checksum、伪造ledger或启动旧二进制。
6. 空库正式迁移仍沿用冻结迁移链并执行新收缩；当前create.sql快照只展示最终现用表。运行代码不保留旧体系，历史迁移文件留存仅用于已执行校验和升级。此轮不再扩展安装基线重建工程。
7. 完成最终结构后，才落实启动配置、最小启用检查、明确少量资源处理范围和真实写入验收；不再执行旧方案的基线封存/激活。

### 7.1 DDL中断与10表删除的具体边界

- 更名状态矩阵：只有旧BASE TABLE存在、歌曲新表不存在时执行RENAME；只有歌曲新表存在时须校验完整定义/保留事实后续跑；两者同时存在或都不存在时拒绝，不合并、不重灌、不伪造成功。三张新表已存在也须核对本表FK、列和索引，不能用IF NOT EXISTS掩盖错误父关联。
- 先检查并建立歌曲resource_id直接FK所需索引及约束，按依赖替换旧唯一/队列索引、删除4条旧归属FK及2条归属触发器，最后删冗余列；policy_origin/local_skip_origin改名对SQL/行映射同时生效。ALTER尽量按表原子执行，中断后核对真实定义，不按“列数大致正确”补ledger。更名及保全核对不改已有uploaded_at/verified_at/updated_at或假造confirmed代次。
- 删除历史资料前先完成源码/SQL依赖退出、备份归档与当前事实保全。10表参考顺序：resource_legacy_operations→resource_legacy_batches→resource_legacy_profiles；resource_discovery_observations→resource_discovery_runs；随后resource_cloud_cleanup_candidates、resource_baseline_blocks、resource_cloud_baselines、resource_cloud_backfills、resource_cloud_migration_audit。执行时仍重新检查实际FK图；不禁用外键检查、不顺带删crawl_batches或保留的transfer_operations。
- 当前零在途/空表是本次观察，不是永久假设。实施前核对三端维护、所有writer停止、slots和文件/producer/旧批次/发现租约无活动或未决。出现新工作先处理其状态再迁移；任何意外非song回执、保留表引用或归档差异都拒绝收缩，不能以“这些历史没有意义”丢弃新事实。
- 两个新migration的分界是DDL恢复与审核边界，不是可混跑新旧程序的部署阶段。业务保持关闭，最终代码要求完整目标版本及四表/退役表后置条件；V3001完成、V3002未完成时不能启动旧二进制或半启新worker。冻结migration及ledger保留，空库完整迁移链与现有V2913升级都须能到最终32表。
- 当前schema/回执读写的所有reader、策略匹配、目标DTO和schema检查使用policy_origin/local_skip_origin。来源字符串及有效skip判定不要求baseline记录，但仍保留版本、启用标记、原因及非空来源校验；不能删baseline时把历史skip顺手解除，或把缺来源的normal行误判为已保存。

## 8. 受影响验证与完成条件

- 歌曲表更名前后91,116行id及保留字段逐值相同，百度/Google各45,558；3张新表为空，FK/唯一键正确；更名不引发下载、上传、id重排或路径变化。
- 原业务/本地路径、旧摘要和45,558个本地skip保持；旧任务三表行数不变，当前producer/related正常功能保留。归档行数/摘要可核对，实际10张表及对应在线依赖退出。
- 全部运行SQL在最终克隆库prepare通过；对四类资源分别验证两端写回/查询、skip先于内容I/O、强确认不被弱结果覆盖、删除级联/别名冲突及事务回滚。固定SQL映射不能错表，原owner触发器移除后真实FK仍防孤儿。
- 对修改的持久化接线做一组有意义的三路回归：local受阻时双云可继续、任一端失败/取消不污染兄弟端、未决及重启认领围栏仍有效。保留MRC/关联子操作的取消与确认行为，不重新跑无关全量采集或既有性能演练。
- 所有正常资源写入口走同一服务；移除旧路由、模式和配置分支，查询只读新表。生产代码不得引用退役表；冻结migration及明确的历史文档引用不计运行依赖。404退役路由和新版请求/响应有针对性契约检查。
- 重复迁移不改变已确认事实或时间戳。最终代码/SQL/表结构一致后，另行进行少量真实写入；本次文档讨论不计作新版本实测通过。

### 本次审查补充的针对性验收

在原受影响验证基础上，一次覆盖以下新增风险；文档审查本身不运行这些测试，不重复无关全量实测：

| 场景 | 必须证明的结果 |
| --- | --- |
| 混合4 owner、三端Process及后段ensure故障 | 有界批量登记，真实task id匹配；任一云表写失败时父操作/任务/槽/全部云写共同回滚 |
| 四表有相同数字的resource/receipt id | 批量状态不串资源，顺序/缺失项/cursor正确，无符号大整数和NULL/空值映射正确 |
| baseline表/列已物理移除，local暂停或maintenance | 百度/Google的受理、claim、current、producer及确认仍可进行；无隐藏的prepare或旧legacyClaimAllowed查询 |
| 音频→关联MRC→两种产物，或显式produce_artifacts | 子操作幂等、实际产物id、父子聚合/定向取消不变；子受理故障无半成品成功，历史skip不被清除 |
| 缺MRC key、父MRC已完成、waiting_input及producer恢复 | 等待和恢复语义不丢失，不自动重放旧crawl任务，不把生成计划等待当全部操作完成 |
| 父删除/音质清理/别名合并遇活动或未决 | guard仍生效且失败共同回滚，覆盖artifact级联和父文件终态下活动producer；不因去capture而丢失保护 |
| 逐步中断rename/约束调整/DROP与重复运行 | 错误半成品明确拒绝或按完整状态续迁；原91,116回执及45,558本地路径/skip不变，三张新回执空表不被误灌 |
| 新库完整迁移、旧库升级与新CLI/schema检查 | 最终32表及四类FK完整；schema/prepare不引用退役表，旧URL不执行handler，当前维护/恢复与三路工厂不依赖旧模式 |

旧历史/兼容工具和专用测试随退役实现退出CMake；保留有价值的统一任务、MRC/关联、取消、容量和回执测试，把共用fixture改为直接建立当前目标/资源/策略，不再通过历史清单封存初始化。过去V2913验收仍作为历史证据，不为维持旧测试而把旧业务实现留在生产目标里。
