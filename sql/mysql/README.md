# MySQL SQL 目录说明

`migrations/` 是当前 MySQL 启动初始化入口。服务启动时会按 `V<version>__<name>.sql` 升序自动应用未执行 migration，并写入 `schema_migrations`。如果 migration 执行失败或已执行版本 checksum 与文件不一致，MySQL schema 初始化失败，服务不可用。

后续 DDL 变更必须新增 migration 文件；查询、插入、更新模板继续放在 `catalog/` 或编号 SQL 中。

根目录下 `01_` 到 `22_` 是当前 Migu 歌曲、资源、歌手、专辑来源入库和同步状态模板，编号表示设计和执行顺序，暂时保持在根目录便于按顺序检查。

`23_` 到 `25_` 是采集控制台批次持久化表：`crawl_batches`、`crawl_batch_items`、`crawl_batch_events`。这三张表不参与音乐元数据去重，只记录任务生命周期、每个目标执行结果和事件流，供前端刷新页面或服务重启后继续查看批次。

`26_` 到 `29_` 是从 `singer_source_refs` 认领待同步歌手歌曲/专辑的模板，筛选条件中的 `country`、`gender`、`q` 为空时不参与过滤，默认同步状态为 `0/3/4`。

`30_` 到 `35_` 是当前元数据参数补强：歌曲歌手关系 `song_artist_refs`、专辑歌曲关系 `album_song_refs`、对应插入模板、缺省 audio 资源任务模板，以及 `crawler_schema_versions` schema 版本表。关系表用于支持多歌手、合唱、Disc/track 曲序和前端 schema/version 检查；`singers` 现在按 `normalized_name` 唯一，只保存公共歌手名和性别，平台拼音保存在 `singer_source_refs.source_name_pinyin`。

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

`tests/` 存放集成测试和真实调试测试使用的校验 SQL，生产代码不应依赖该目录。
