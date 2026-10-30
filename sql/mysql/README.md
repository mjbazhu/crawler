# MySQL 表职责与执行顺序

运行期 SQL 按所属表组织，例如 `songs/upsert.sql`、`song_source_refs/upsert.sql`、`album_artist_refs/upsert.sql`。每个写入模板只修改自己的表，可以通过 JOIN 查询其他表。歌曲 SQL 不创建歌手或专辑，专辑 SQL 不回填歌曲关系。

## 表职责

| 表 | 保存内容 | 去重或关联方式 |
| --- | --- | --- |
| `songs` | 公共歌曲标题、标准化标题、公共标识 | `normalized_title` |
| `song_source_refs` | 平台歌曲身份、内容 ID、资源类型、歌曲拼音、时长、版权 ID | 来源业务键，关联 `songs` |
| `singers` | 公共歌手名称、标准化名称 | `normalized_name` |
| `singer_source_refs` | 平台歌手身份、拼音、地区、声明总数及采集状态 | 平台与歌手 ID，关联 `singers` |
| `singer_index_memberships` | 分类 tab、标签、顺序和指标 | 歌手来源主键与 tab |
| `albums` | 专辑标题、标准化标题、发行日期、稳定身份 hash | `SHA256(source + '|' + source_album_id)` |
| `album_source_refs` | 平台专辑身份、资源类型、专辑类型、拼音、简介、声明歌曲总数、采集状态 | 平台与专辑 ID，关联 `albums`；标题只由 `albums` 保存 |
| `song_artist_refs` / `album_artist_refs` | 歌曲/专辑来源与歌手来源之间的角色、顺序、主歌手和入口标记 | 两端主键与角色 |
| `album_song_refs` | 专辑来源与歌曲来源之间的 Disc、track、顺序 | 两端主键，均不可为空 |
| `song_source_audio_formats` | 音质、资源类型、iOS/Android 格式编码及大小 | 歌曲来源主键与音质 |
| `song_resources` | 歌曲资源任务、本地文件快照、状态、错误及内容/来源代次 | 歌曲来源主键、资源类型、音质 |
| `singer_resources` / `album_resources` | 图片 URL、本地回执、状态、认领及内容/来源代次；云端事实在统一回执表 | 所属来源主键与资源类型 |
| `song_resource_artifacts` | MRC 派生产物、版本、本地结果及内容/来源代次；云端事实在统一回执表 | 父资源、产物类型与版本 |
| `resource_cloud_receipts` | 四类文件资源在 baidu/gdrive 的当前任务、最后确认回执、核验证据及独立跳过策略 | owner_type、resource_id、cloud_provider 唯一；四条原生级联外键及 BEFORE 身份触发器 |
| `resource_transfer_operations` | 首次受理的父操作、幂等摘要与请求快照 | 不随配置变化重新展开缺省目标 |
| `resource_transfer_tasks` | 逐资源逐端的冻结版本、预算、认领、意图与终态 | 每操作最多 600 项；历史不随资源级联删除 |
| `resource_transfer_slots` | 当前同资源目标的独占占用 | busy 不接管取消权；未决对象保留占用直到显式修复 |
| `resource_legacy_profiles` | 首次显式启用的固定旧单云视图 | profile_key 唯一；关闭传输不更换历史平台 |
| `resource_legacy_batches` | 旧完成范围、并发/总截止及不可变终态 | 对应 crawl_batches；活跃索引供跨重启观察 |
| `resource_legacy_operations` | 一个旧批次到多个真实操作的持久映射 | 每 200 资源分块；唯一 operation_id 防重复投影 |
| `resource_producer_tasks` | 每个原 MRC 目标任务的独立生成计划、处理版本、预算、认领和子操作 ID | 父 task 唯一；父操作与目标状态索引，生成不依赖父传输 running |
| `resource_transfer_related` | 音频合法关联类型、认领代次与受控子操作 | 每父task唯一；与确认同事务受理，不保存临时URL |
| `resource_destination_states` | local/baidu/gdrive 目标修订、身份指纹、维护状态、基线就绪及队列限额/版本 | destination 主键；当前新增工具保持维护关闭 |
| `resource_cloud_cleanup_candidates` | 删除前显式捕获的逐端对象版本、路径摘要与确认事实 | receipt/snapshot去重，无资源FK；预演重核引用和意图，不执行远端删除 |
| `resource_cloud_backfills` / `resource_cloud_migration_audit` | 固定清单、逐端游标/计数与逐资源历史证据/冲突 | run 与 manifest 绑定；页事务原子提交，删除资源不删除审计 |
| `resource_discovery_runs` | 固定四类资源范围、目标/库身份、路径映射、计数和页租约 | run_id与定义摘要绑定；每目标一个running；暂停/取消推进代次 |
| `resource_discovery_observations` | 精确路径的只读元信息证据与来源快照 | run/id主键、页提交原子；仅关联run，资源删除保留观察，不改变skip/回执 |
| `resource_cloud_baselines` | 四类已审阅范围、目标版本/指纹、封存摘要与激活状态 | baseline ID 与固定 JSON/digest 绑定，审核后事务内再校验 |
| `resource_baseline_blocks` | 已排除历史范围及别名冲突的禁止隐式重传策略 | owner/id/content_generation 唯一；不随资源删除而清空审核意图 |
| `migu_dalbum_details` | 数字专辑来源与 `materialId` 的明确映射 | 专辑来源主键 |
| `crawl_batches` / `crawl_batch_items` / `crawl_batch_events` | 任务、目标和事件 | 与音乐实体去重分离 |
| `governance_events` | 治理操作记录 | 事件主键 |

`song_catalog_sources`、`album_catalog_sources`、`singer_catalog_sources`、`album_song_catalog`、`album_catalog` 是只读视图。展示所需的名称和平台 ID 从实体及关系实时派生，不重新存储冗余字段。写入和删除必须针对真实表。

三个来源表不保存 `source_item_key`。兼容查询按需生成 `singer:{source_singer_id}`、`album:{source_album_id}`；歌曲优先生成 `content:{source_content_id}`，没有内容 ID 时生成 `song:{source_song_id}`。歌曲保留两组来源 ID 唯一索引，并通过数据库 CHECK 与入库校验保证至少有一个有效外部 ID。专辑接口的 `source_title` 仅为 `albums.title` 的兼容别名。

专辑 `album_songs_count` 是当前 `album_song_refs` 中的关系数量，`songs_count` 是其展示别名；`songs_sync_count` 读取 `album_source_refs` 中最近一次专辑曲目采集成功处理条数。关系可由歌手歌曲等不同入口写入，完成情况只读取 `songs_sync_status`，不根据已有关系数量推断。详情同步与歌曲同步继续保留各自的状态、时间和错误。

歌曲、专辑和资源队列的常用查询直接读取真实表，先按索引分页，再为本页读取名称、关系和资源统计。兼容视图保留给已有查询入口，避免常用分页先展开全部关系或聚合全部资源。

## 首次采集的固定流程

1. parser 从同一份响应提取歌曲、专辑、歌手与关系片段；传输对象可以携带响应中的关联信息。
2. `MetadataBatchBuilder` 按来源身份合并实体输入，把歌曲携带的专辑名称、拼音分到专辑输入，把歌手名称、拼音分到歌手输入。
3. application 在数据库事务外完成必要的元数据请求和解析；歌手、专辑封面仅随响应保存 URL，采集阶段不下载封面或歌词。
4. `MySqlMetadataBatchWriter` 开启一个事务，依次调用歌手、专辑、歌曲各自 SQL，取得来源主键。
5. 用主键写分类归属、歌曲歌手、专辑歌手和专辑歌曲关系，再写响应携带的音质参数、资源登记和歌手/专辑封面 URL。
6. 任一实体或关系失败，整批回滚；成功则提交。空字段不覆盖已知属性，缺失端点不能保存为空关系；已有资源文件和处理状态不因更新 URL 被重置。

搜索、URL 采集、歌手歌曲、歌手专辑和专辑详情使用同一 MySQL 写入入口。专辑详情与详情内的歌曲一次提交。deprecated/SQLite 兼容装配保留旧 port，MySQL 生产装配使用事务 writer。

专辑来源 ID 从列表到详情保持稳定。改名、补充歌手、拼音、日期或 `materialId` 不改变 hash；不同来源 ID 不凭名称合并，数字专辑物料 ID 单独保存映射。

## 本次结构清理

- `song_source_refs` 删除 `source_album_id`、`source_album_name`、`source_album_pinyin`、`cover_url`、`lyric_url`、`mrc_url`、`trc_url`。
- `song_resources` 删除 `remote_url`。地址只在当前下载调用中传递，资源队列重试重新向平台获取。
- `albums` 删除 `primary_artist_name`，歌手由 `album_artist_refs` 关联获得。
- `album_song_refs` 删除 `source`、`source_song_id`、`source_album_id`、`source_content_id`。
- `album_source_refs` 删除主歌手 ID/名称、封面 URL、跳转 action、重复列表类型、日期和播放数字段；`total_count` 改为可空整数 `source_song_total`，拼音使用 `source_title_pinyin`。
- `singer_source_refs` 删除 `avatar_url` 和重复的 `source_tag`；封面归资源表，分类标签归 membership。
- `migu_dalbum_details` 删除重复专辑来源 ID，以及价格、会员、售卖期等不参与采集的字段，仅保留物料身份映射。

`song_source_audio_formats` 的 `iformat/aformat/isize/asize` 本轮保留，分别表示 iOS/Android 格式和大小。详情字段 `iosFormat/androidFormat/iosSize/androidSize` 与列表旧字段统一解析。

## Schema 管理与实施计划

启动初始化只通过 `migrations/` 升序执行未应用版本，在 `schema_migrations` 记录 checksum。历史 migration 不修改；`V2026102501__normalize_music_table_ownership.sql` 规整表职责，`V2026102502__optimize_normalized_catalog_queries.sql` 增加来源与更新时间组合索引，并将已知历史专辑类型转换为详情请求所需的资源类型。`V2026102601__source_identity_and_album_count_semantics.sql` 删除来源派生业务键和重复专辑标题、增加歌曲身份约束、修正专辑 hash 注释，并重建兼容查询视图；已有同步状态及处理数量保持不变。

迁移先保存旧拼音和关系端点、建立专辑歌手关系，再删除旧列。已有本地文件路径、云端路径和完成状态保留，不清空音乐表或资源表。MySQL DDL 会隐式提交，失败不代表整文件回滚；多数步骤具备存在性判断，重试前仍须核对结构与 ledger。执行整个文件必须使用同一连接。

无需启动业务服务即可使用 `crawler_cloud_receipts schema-status/schema-migrate/schema-check-sql` 完成结构升级和 SQL prepare，详见[独立数据库迁移](../../docs/operations/resource-database-migration.md)。Runner 在全部已知 checksum 核实后同步组件版本与 ledger；同版本重跑保持时间戳。V2910 仅在冻结 checksum 及完整 DDL 后置条件匹配时恢复未登记记录，部分结构不自动修补。

已有大库首次升级时，可设置 `CRAWLER_MYSQL_STARTUP_TIMEOUT_MS=600000`，为数据迁移和索引创建预留时间。后续启动根据 migration 记录跳过已完成版本。

`V2026102801__singer_profile_field_cleanup.sql` 删除公共名称池的 `gender/status`、对应无效状态索引和歌手来源的 `metadata_status`；来源类型、地区及目录生命周期原样保留，重建来源查询视图。独立资料刷新复用事务 writer，只更新响应已有的资料与声明总数。

各表 `create.sql` 是当前结构快照，用于查阅与独立建表；正式升级仍走 migration。`transactions/` 放事务控制，`schema/` 放跨表 schema/完整性检查，`tests/` 仅用于校验。

`V2026102802__metadata_cover_download_claims.sql` 为两类封面补 `claim_generation/running_batch_id/claimed_at` 和队列索引。封面迁移在发布前从 `2026102801` 避让到 `2026102802`，SQL 内容及 checksum 不变，避免与并行歌手资料专项已执行的 `singer_profile_field_cleanup` 冲突；不修改已有歌手清理记录。`cover_*` SQL 只负责对应资源表的查询、条件认领、成功/失败及超时恢复；成功回写同时比较代次与 URL。失败和恢复保留已有完整文件。专辑别名合并保留规范来源的活跃认领，迁入物料来源时不继承原 worker 的运行态；恢复日志同时核对已经迁移到其他主键的文件引用。

`V2026102901__metadata_cover_cloud_storage.sql` 是两类封面旧单端字段的历史来源。当前 `cloud_*` 模板通过显式 provider 读写 `resource_cloud_receipts`，本地快照仍读取资源表；成功确认同时核对内容/来源/目标和本地认领，失败保留最后确认对象。`cover_process_*` 保留旧候选接口，专辑别名归一只在参考身份相符时迁移缺少的逐端回执，不覆盖规范资源已有事实。

当前源码最新可用版本为 `2026102913`；实际开发 `crawler` 的本轮收缩执行状态见[数据库迁移记录](../../docs/operations/resource-database-migration.md#v2913-物理云列收缩状态)。`V2026102902__resource_cloud_receipts_expand.sql` 增加四类资源代次、参考摘要、本地策略及双端回执。`V2026102903` 增加三张持久任务表和 artifact 原生认领字段；`V2026102904/05` 捕获来源变化并按字节比较身份，`V2026102906` 保留旧版本本地确认；`V2026102907` 增加基线封存与排除策略。扩展阶段保留旧列；新增 V2913 在原值封存后删除旧物理列。原44份 migration 保持字节及 checksum 不变。工具 `baidupcs/rclone` 与平台 `baidu/gdrive` 分开，rclone 平台必须明确配置。四个 nullable 外键与 owner/id 由 BEFORE 触发器核对；迁移专用分句器支持 `DELIMITER`，触发器需要相应权限。持久任务、确认及恢复边界见[执行说明](../../docs/operations/resource-transfer-tasks.md)。

`V2026102913__retire_resource_cloud_columns.sql` 不新增表：先将有旧值的资源精确封存在 `resource_cloud_migration_audit`，run 为 `schema-retirement-v2026102913`，再删除歌曲3列、两类封面各13列、派生产物2列，共31个旧物理 `cloud_*` 列及2个旧封面云队列索引。本地路径/摘要/状态和业务数据保留；云事实只从 `resource_cloud_receipts` 读写。旧 HTTP JSON 字段继续作为新表投影，`resource_legacy_profiles/batches/operations` 三张映射表保留，不属于本次 DROP。封存标记支持分表 DDL 中断重放；条件、验证和旧协议整体退役的区别见[收缩说明](../../docs/operations/resource-schema-retirement.md)。

`schema_status.sql` 除版本/checksum、表/列外还检查回执/任务/基线约束、两个身份触发器和八个来源触发器，缺失项返回 `missing_constraints` 并使 ok=false。历史迁移文件未修改；V2902 checksum 为 `6718eaecb20a485a6a16b22fe88c4b9e`，V2907 为 `c6e2256fa2e53f999a92588f84e583fd`。快照 `create.sql` 不单独安装身份触发器，正式结构仍须执行 migration。

历史声明导入见[维护 CLI](../../docs/operations/resource-cloud-import.md)：资源快照按固定高水位分批读取，逐页锁定并比较来源/旧值/代次；两端 skip、本地 skip、审计与双端 cursor 同事务落库。旧校验值原样保留，人工声明不制造摘要和核验时间。歌曲/封面用例已通过 `transfer_admission.sql` 读取当前策略，MySQL 写入口通过 schema 与目标状态检查失败关闭。`writes_allowed.sql` 阻止维护/无就绪活动目标，`destination_ready.sql` 区分单端暂停，local 关闭不由另一端代替。

四类 owner 均须有完整、零冲突的双端导入 run；未确认的范围用显式 exclude-all 清单保留逐行排除，不能扩大歌曲的双端声明。基线 review 只读复核当前快照、范围计数与策略，seal 在相同事务再次复核并写入按代次的 block，activate 再核对固定摘要和目标身份后原子设置三端 active/paused。原生运行态、持久槽、未决任务或回执均阻止封存。不能手改 ready/state 代替该闭环；目标变化和新增范围必须重新审查。

专辑别名合并在 metadata 事务中保护双方持久占用，先归档旧事实再删除别名。只有完整参考 SHA-256/大小一致时迁移缺少的逐端回执和 local skip，原目标修订保持不变，规范资源的已有回执不覆盖。参考冲突或缺失将新代次纳入 block；旧排除策略同步迁入规范资源。归档包括旧路径、版本、核验及策略，相关旧文件保留；此流程不执行远端删除或通用孤儿回收。

`V2026102908__durable_resource_producers.sql` 增加独立生成计划；enqueue 与原 MRC 文件任务同事务冻结 producer 配置，普通 process 的历史 skip/busy 不生成；显式 produce_artifacts 仅允许 MRC，以 producer_only 父项保留原 skip/回执且不建原文件传输槽，仍受版权、基线排除和目标围栏保护。缺 key 持久为 waiting_input；认领使用 producer 自身代次和预算，父 MRC 可以已经成功或失败。有效输入上限由配置/仓储/worker 收紧至 1 MiB，表级 CHECK 的较宽上限不代表运行能力。认领、取消、失败、有限退避及单页恢复均可独立查询，不回写父传输成功或失败。

`MySqlResourceProducerTasks::complete` 先核对 producer 自身认领、来源/内容/目标、处理规则及取消，再复用 `MySqlProducedArtifactRegistry` 的事务内 helper 绑定实测 MRC 参考和两产物身份；同一事务通过 `enqueueOnConnection` 建立逐端子操作/任务并记录 producer 成功。任一子受理失败整笔回滚，消除“已登记但漏排任务”窗口。它不写正式 local_path、local checksum/size 或成功状态。同身份不同输出整体拒绝；身份升级需无活跃/未决占用，保留上一代本地与云端确认。

操作聚合包含本操作文件任务、producer 与直属子操作，并沿祖先最多两层刷新。修改按 root→child 操作锁、排序资源、目标和任务顺序进行，父取消仍覆盖父传输已成功但 producer/产物尚未结束的情形。关联发现和产物子操作使用同一事务 helper，旧兼容批次可在一个外事务内分块受理而不扩大单操作 200 资源/600 任务上限。索引成功或 producer 成功均不代表产物已经保存到正式本地或云端。

歌曲 `claim_select/claim_update/related_claim_update` 均保护匹配的本地 skip 与当前代次 block；正式 `PersistenceAssembly` 设置 `SqlPaths.enforce_dispatch=true`，在认领 SQL 本身再次检查全局维护及 local 就绪。低层 adapter 的默认构造保留旧独立调用语义；使用自定义装配的调用者必须注入准入并启用该标志。模板新增对应绑定参数，需与同版本 adapter 一起部署，不能混用旧二进制和新 SQL。生产切换和收缩仍按[方案](../../docs/roadmap/resource-cloud-receipts.md)逐项验收，不能仅以 migration 或基线命令成功放行生产。运行与升级失败处理见[恢复指南](../../docs/operations/service-lifecycle.md)。

封面确认 SQL 返回零受影响行与执行报错必须区别：前者是明确条件拒绝；后者只有重读到匹配的成功状态、路径、大小和摘要才可确认成功，未命中或不同快照仍保持未知。原主键可能因专辑别名归一被删除，在途提交也可能尚不可见；不能把此类重读当作“明确未提交”并回收新文件。本地未知保留文件/journal，云端未知待重新核对，恢复时继续按代次和文件引用判断。本轮修复在 adapter 的确认逻辑，不修改已执行 migration 或表级条件 SQL。

上述封面单端阶段的实施顺序为：新增 migration 与表级 SQL → 接入原子编排和封面存储 → 更新查询、资源队列和协议 → 独立库验证 → 常规部署时由启动初始化执行迁移。V2902 所属统一回执阶段另按方案的停写、桥接、导入封存及独立收缩推进，不能沿用单端阶段的直接启动升级步骤。

验证覆盖历史结构升级及重跑、拼音与关系保留、首次写入、幂等、详情不改变身份、合唱、多 tab、本地封面结果、失败回滚，以及全部运行期 SQL prepare。迁移集成测试必须显式使用独立 `crawler_ownership_test_*` 数据库。

批量状态通过 resource_cloud_receipts/status_batch.sql 以最多200个完整身份的JSON_TABLE驱动主键联表，同一语句读取四类资源、本地及两端回执。缺失身份按序保留，不发锁定/写入/存储探测请求；共享单条回执映射规则。无需新增表或migration。baseline-maintenance复用既有目标、基线和governance_events，在相同事务关闭准入/退回sealed/登记审计，保留占用与历史事实；激活仍走完整重核。

`V2026102912` 为 resource_cloud_backfills 增加 control_state/control_generation，独立区分页进度与操作者暂停/取消，并新增 resource_cloud_cleanup_candidates 保存删除前显式捕获的不可变证据（无资源级联FK）。单端采信复用 backfills/audit/receipts；补充基线固定逐端run和控制代次。V2912 扩展时保留旧列，V2913 单独收缩物理云列；远端删除和全部旧协议退役另行处理。

旧治理 retry/reset/recover SQL 在候选与实际更新阶段保护当前 local skip、基线排除、三端独占槽和未决回执；reset/retry 保持仅重置旧状态、不登记传输。父实体删除、音质占位清理和专辑别名合并复用事务内捕获：当前双端事实读取新回执，历史云值读取不可变 V2913 档案；占用或审计失败共同回滚。waiting_input 任务取消与 pending/retry 同分支，兄弟端仍独立；有未知 attempt 路径仍保持 uncertain。历史导入审计使用有界 JSON_TABLE 批量 INSERT，每块最多4MiB，全部分块与本地策略、双端回执及游标仍在同一页事务，不改冻结迁移。
