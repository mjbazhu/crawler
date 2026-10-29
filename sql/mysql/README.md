# MySQL 表职责与执行顺序

运行期 SQL 按所属表组织，例如 `songs/upsert.sql`、`song_source_refs/upsert.sql`、`album_artist_refs/upsert.sql`。每个写入模板只修改自己的表，可以通过 JOIN 查询其他表。歌曲 SQL 不创建歌手或专辑，专辑 SQL 不回填歌曲关系。

## 表职责

| 表 | 保存内容 | 去重或关联方式 |
| --- | --- | --- |
| `songs` | 公共歌曲标题、标准化标题、公共标识 | `normalized_title` |
| `song_source_refs` | 平台歌曲身份、内容 ID、资源类型、歌曲拼音、时长、版权 ID | 来源业务键，关联 `songs` |
| `singers` | 公共歌手名称、标准化名称、性别 | `normalized_name` |
| `singer_source_refs` | 平台歌手身份、拼音、地区、声明总数及采集状态 | 平台与歌手 ID，关联 `singers` |
| `singer_index_memberships` | 分类 tab、标签、顺序和指标 | 歌手来源主键与 tab |
| `albums` | 专辑标题、标准化标题、发行日期、稳定身份 hash | `SHA256(source + '|' + source_album_id)` |
| `album_source_refs` | 平台专辑身份、资源类型、专辑类型、拼音、简介、声明歌曲总数、采集状态 | 平台与专辑 ID，关联 `albums`；标题只由 `albums` 保存 |
| `song_artist_refs` / `album_artist_refs` | 歌曲/专辑来源与歌手来源之间的角色、顺序、主歌手和入口标记 | 两端主键与角色 |
| `album_song_refs` | 专辑来源与歌曲来源之间的 Disc、track、顺序 | 两端主键，均不可为空 |
| `song_source_audio_formats` | 音质、资源类型、iOS/Android 格式编码及大小 | 歌曲来源主键与音质 |
| `song_resources` | 歌曲资源任务、已保存文件/云端结果、状态和错误 | 歌曲来源主键、资源类型、音质 |
| `singer_resources` / `album_resources` | 图片 URL、本地封面回执、状态和脱敏错误；独立下载的代次、批次与认领时间 | 所属来源主键与资源类型 |
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

迁移先保存旧拼音和关系端点、建立专辑歌手关系，再删除旧列。已有本地文件路径、云端路径和完成状态保留，不清空音乐表或资源表。DDL 阶段具备存在性判断，可以在中断后重试；执行整个文件必须使用同一连接。

已有大库首次升级时，可设置 `CRAWLER_MYSQL_STARTUP_TIMEOUT_MS=600000`，为数据迁移和索引创建预留时间。后续启动根据 migration 记录跳过已完成版本。

各表 `create.sql` 是当前结构快照，用于查阅与独立建表；正式升级仍走 migration。`transactions/` 放事务控制，`schema/` 放跨表 schema/完整性检查，`tests/` 仅用于校验。

`V2026102802__metadata_cover_download_claims.sql` 为两类封面补 `claim_generation/running_batch_id/claimed_at` 和队列索引。封面迁移在发布前从 `2026102801` 避让到 `2026102802`，SQL 内容及 checksum 不变，避免与并行歌手资料专项已执行的 `singer_profile_field_cleanup` 冲突；不修改已有歌手清理记录。`cover_*` SQL 只负责对应资源表的查询、条件认领、成功/失败及超时恢复；成功回写同时比较代次与 URL。失败和恢复保留已有完整文件。专辑别名合并保留规范来源的活跃认领，迁入物料来源时不继承原 worker 的运行态；恢复日志同时核对已经迁移到其他主键的文件引用。

`V2026102901__metadata_cover_cloud_storage.sql` 为两表增加 `cloud_backend/path/size_bytes/checksum/status/uploaded_at`、独立云端认领、错误和 JSON 上传意图。`cloud_*` SQL 只修改云端列；成功确认需本地状态 2、路径/大小/SHA-256 快照与云端代次匹配。`cover_process_*` 把本地 0/3 与云端缺失、失败、待核对或配置变化的本地 2 项纳入候选。已确认回执匹配时不再处理；本地刷新使旧云端认领失效，保留最后确认对象作为历史回执。专辑别名合并同时迁移所选本地文件及云端回执，不继承旧云端运行态。百度和 Google 共用该结构，只有 `cloud_backend/path` 不同。

实施顺序：新增 migration 与表级 SQL → 接入原子编排和封面存储 → 更新查询、资源队列和协议 → 独立库验证 → 常规部署时由启动初始化执行迁移。

验证覆盖历史结构升级及重跑、拼音与关系保留、首次写入、幂等、详情不改变身份、合唱、多 tab、本地封面结果、失败回滚，以及全部运行期 SQL prepare。迁移集成测试必须显式使用独立 `crawler_ownership_test_*` 数据库。
