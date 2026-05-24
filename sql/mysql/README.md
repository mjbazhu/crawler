# MySQL SQL 目录说明

`migrations/` 是当前 MySQL 启动初始化入口。服务启动时会按 `V<version>__<name>.sql` 升序自动应用未执行 migration，并写入 `schema_migrations`。如果 migration 执行失败或已执行版本 checksum 与文件不一致，MySQL schema 初始化失败，服务不可用。checksum 使用 `MySqlMigrationRunner::checksumOf()` 的稳定算法，不是 MD5。

后续 DDL 变更必须新增 migration 文件；查询、插入、更新模板继续放在 `catalog/` 或编号 SQL 中。

根目录下 `01_` 到 `22_` 是当前 Migu 歌曲、资源、歌手、专辑来源入库和同步状态模板，编号表示设计和执行顺序，暂时保持在根目录便于按顺序检查。

`23_` 到 `25_` 是采集控制台批次持久化表：`crawl_batches`、`crawl_batch_items`、`crawl_batch_events`。这三张表不参与音乐元数据去重，只记录任务生命周期、每个目标执行结果和事件流，供前端刷新页面或服务重启后继续查看批次。

`26_` 到 `29_` 是从 `singer_source_refs` 认领待同步歌手歌曲/专辑的模板，筛选条件中的 `country`、`gender`、`q` 为空时不参与过滤，默认同步状态为 `0/3/4`。

`30_` 到 `35_` 是当前元数据参数补强：歌曲歌手关系 `song_artist_refs`、专辑歌曲关系 `album_song_refs`、对应插入模板、缺省 audio 资源任务模板，以及 `crawler_schema_versions` schema 版本表。关系表用于支持多歌手、合唱、Disc/track 曲序和前端 schema/version 检查；`singers` 现在按 `normalized_name` 唯一，只保存公共歌手名和性别，平台拼音保存在 `singer_source_refs.source_name_pinyin`。`song_artist_refs` 只保存歌曲与歌手来源关系，歌手头像、拼音等属性不再放入关系表。

`common/` 存放跨入库模板复用的事务 SQL。

`catalog/` 存放通用 Song、Album、Singer 仓储的 CRUD、内容查询、补池治理、资源队列治理、schema 状态和治理审计 SQL 模板。这些模板由 repository 调用，不应写入 controller 或 application。

`migrations/` 存放正式 DDL migration：

- `V2026052001__baseline_current_schema.sql`：当前完整 schema 基线，幂等创建核心表。
- `V2026052101__resource_error_fields.sql`：补齐资源队列错误分类和批次关联字段。
- `V2026052102__governance_events.sql`：新增治理审计表。
- `V2026052103__batch_archive_and_summary.sql`：补齐批次归档、父批次追溯和 summary 索引。
- `V2026052104__song_source_duration_and_trc.sql`：把平台时长迁移到 `song_source_refs`，并补齐 `trc` 歌词资源参数。
- `V2026052105__song_title_pool_and_source_ref_cleanup.sql`：把 `songs` 收敛为按 `normalized_title` 唯一的标题池，并清理 `song_source_refs` 中不再需要的版权类型和授权原始字段。
- `V2026052106__song_artist_singer_ref_and_source_cleanup.sql`：给 `song_artist_refs` 补齐 `singer_source_ref_id/is_seed`，并移除 `song_source_refs` 中主歌手冗余字段。
- `V2026052107__singer_source_refs_cleanup.sql`：把 `singers` 收敛为按 `normalized_name` 唯一的公共歌手池，迁移平台拼音到 `singer_source_refs`，并移除来源表中的歌手名和性别冗余字段。
- `V2026052108__singer_source_summary_recount.sql`：按 `song_artist_refs` 和 `album_source_refs.source_artist_id` 重算歌手歌曲/专辑汇总，修正已有内容但状态仍待处理的来源记录。
- `V2026052109__song_artist_refs_relationship_only.sql`：把 `song_artist_refs` 收敛为纯歌曲-歌手关系表，删除平台来源、头像和拼音冗余字段，并调整关系去重约束。
- `V2026052110__singer_index_memberships.sql`：新增歌手分类归属表，记录唯一歌手来源出现在哪些 Migu 分类 tab 中，避免在 `singer_source_refs` 中重复保存同一平台歌手。
- `V2026052111__song_artist_refs_strict_singer_relation.sql`：把 `song_artist_refs` 进一步收敛为强关系表，要求 `singer_source_ref_id` 非空并删除平台歌手 ID/名称冗余字段。
- `V2026052402__migu_song_cover_url_prefix.sql`：补齐历史 Migu 歌曲封面相对路径的请求前缀，并同步修复 `song_resources` 中的 cover 远程地址。
- `V2026052403__migu_text_resource_queue_cleanup.sql`：清理历史 Migu 空封面/歌词资源占位，并恢复被 440015/440016 连带跳过的封面、歌词、mrc、trc 资源任务。

`tests/` 存放集成测试和真实调试测试使用的校验 SQL，生产代码不应依赖该目录。
