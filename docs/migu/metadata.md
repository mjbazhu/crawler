# Migu 元数据模型与关系

元数据 parser、事务 writer、catalog/backfill、实体关系、资源表和 migration 改动前读取本文；资源下载状态还需读取 [资源链路](resources.md)。完整表职责和事务顺序见 [MySQL 说明](../../sql/mysql/README.md)，此处维护修改时需要遵守的不变量。

## Migu 元数据关系表维护上下文

本文记录当前 Migu 歌手、歌曲、专辑元数据入库和控制台查询的关系设计。修改 `singers`、`singer_source_refs`、`songs`、`song_source_refs`、`song_artist_refs`、`albums`、`album_source_refs`、`album_song_refs`、`song_resources` 或相关 catalog/backfill SQL 时必须先阅读本文。

### 歌曲与来源表

- `songs` 当前作为公共歌曲标题池使用，最核心字段是 `title` 和 `normalized_title`；`normalized_title` 应保持唯一，来源表通过 `song_db_id` 复用标题记录。
- 平台差异字段必须留在 `song_source_refs` 或更具体的来源表中；`duration_seconds` 属于平台来源时长，不应放回公共 `songs`。
- `song_source_refs.source_title_pinyin` 属于 Migu 来源拼音，保留在 refs；不要移动到公共 `songs`。
- `song_source_refs.copyright_id` 目前作为资源链路兼容参数保留；`copyright_type`、`restrict_type`、`product_authorize_usage` 这类暂不服务采集和资源处理的字段不应继续扩大使用，后续确认资源链路不需要后可清理。
- `song_source_refs` 不保存专辑属性及封面、歌词远端地址。解析输入中的专辑名称与拼音在首次写入时分到 `albums/album_source_refs`，拼音使用 `album_source_refs.source_title_pinyin`。

### 歌曲、歌手和专辑关系

- `song_artist_refs` 是歌曲来源到歌手来源的关系表，只表达关系，不保存歌手属性。不得重新加入 `source`、`source_artist_image`、`source_name_apelling` 或已能从 `singer_source_refs` 得到的歌手字段。
- `song_artist_refs` 以 `song_source_ref_id`、`singer_source_ref_id`、`artist_role` 表达唯一关系；按歌手入口采集歌曲时，应把入口歌手对应的 `singer_source_refs.id` 写入 `singer_source_ref_id`，并标记 `is_seed=1`，主唱可标记 `is_primary=1`。
- Migu 歌手索引已整体入库后，`singerList` 中的歌手应优先通过 `source_artist_id` 匹配 `singer_source_refs` 再写关系；不要再把 `source_artist_id/name` 作为关系表持久字段兜底。
- `album_song_refs` 只保存非空的 `album_source_ref_id`、`song_source_ref_id`、disc、track 和来源顺序，平台身份通过两端来源表取得。
- 歌手歌曲响应已提供专辑身份和名称时，直接创建专辑摘要，再写专辑歌曲关系，不额外发详情请求。实体创建、关系、音质与资源结果在同一数据库事务提交。
- `album_artist_refs` 维护专辑与歌手来源的角色、顺序、主歌手和入口标记；`albums` 与 `album_source_refs` 不重复保存歌手属性。专辑身份 hash 使用平台与专辑来源 ID，不依赖名称、歌手或后到的物料 ID。
- `singer_resources`、`album_resources` 保存采集时实际下载的本地封面结果，不保存 source 或远端 URL；数据库事务失败时清理本次新增封面。

### 歌手索引与 tab 归属

- `singers` 是公共歌手名称池，核心字段是 `name` 和 `normalized_name`；`normalized_name` 应作为去重键。平台拼音、来源 ID、来源国家/地区、来源标签和头像应在来源表或索引归属表中维护。
- `singer_source_refs` 表达 Migu 歌手来源身份；同一 `source_singer_id` 可能出现在多个 tab/分类下，不应因为主来源唯一而丢失分类归属。
- tab、分类、性别分组、国家分组这类索引入口归属应由独立 membership/索引归属表记录；不要通过重复插入 `singer_source_refs` 来表达多 tab 归属。

### 控制台查询和计数

- 控制台列表和 overview 需要展示事实计数时，实时从关系表聚合：歌手歌曲数来自 `song_artist_refs`，歌手专辑数来自 `album_artist_refs`，专辑歌曲数来自 `album_song_refs`。
- 前端展示别名可以返回 `songs_count`、`albums_count`、`album_songs_count`，但这些是查询 DTO/API 层别名，不应新增写入时强维护的 summary 表。
- 专辑 `album_songs_count` 是当前关系数量，`songs_sync_count` 是最近一次专辑歌曲采集成功处理条数，必须分开读取与返回。专辑完成情况按 `album_source_refs.songs_sync_status` 统计，不能由已有关系数量覆盖；歌手歌曲入口可以创建关系，但不会因此完成专辑曲目采集。
- 三个来源表不保存派生的 `source_item_key`，兼容接口按平台 ID 计算；歌曲仍保留歌曲 ID、内容 ID 两组唯一约束及至少一个有效 ID 的数据库 CHECK。专辑标题只写 `albums.title`，`source_title` 仅可作为兼容查询别名。
- 数量过滤参数应跟事实计数一致：歌手支持 `min_songs_count/max_songs_count/min_albums_count/max_albums_count`；专辑支持 `min_songs_count/max_songs_count`，可兼容 `min_album_songs_count/max_album_songs_count`。
- 当真实关系已存在且没有同步错误时，歌手歌曲/专辑队列可以按完成状态展示；但 `1 running` 仍必须保持运行态，不能被事实计数覆盖。

### Migration 和 schema 状态

- MySQL DDL 以后进入 `sql/mysql/migrations/`；已执行过的 migration 文件不得修改内容，否则会触发 checksum mismatch。
- 运行期 SQL 按表放入 `sql/mysql/<table>/`，每个写入模板只修改本表；跨表顺序由事务 writer 编排。各表 `create.sql` 是结构快照，启动升级仍使用 migrations。
- 如果真实环境出现 checksum mismatch，应新增修复 migration 或提供明确的人工修复说明，不要直接改旧 migration 来“对齐”。
- `schema.sql` 或旧编号 SQL 只能作为 legacy/测试参考入口；生产初始化以 migration runner 为准。
- 新增/删除字段必须同步更新 schema status 检查、OpenAPI 文档、catalog/backfill SQL 和对应测试。
