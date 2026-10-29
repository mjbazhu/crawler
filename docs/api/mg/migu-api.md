# Migu 元数据 API 对接说明

当前 HTTP API 覆盖 Migu 元数据、歌曲资源处理，以及歌手头像/专辑封面本地下载与配置驱动的云端存储：歌手、歌手歌曲、歌手专辑、专辑详情、专辑歌曲、歌曲音频和歌词资源。当前契约版本为 `1.9.0`。

OpenAPI：

- 运行时 JSON：`GET /api/openapi.json`
- 静态 YAML：[openapi-migu.yaml](./openapi-migu.yaml)

## 前端推荐流程

1. 打开控制台先调用 `GET /api/migu/overview` 显示库内聚合状态。
2. 调用 `GET /api/migu/integrity` 判断哪些元数据还没跑完，是否存在 stale running。
3. 调用 `GET /api/migu/schema/status` 检查 MySQL schema 版本和关键表/列是否完整。
4. 如发现 stale running，可调用 `POST /api/migu/maintenance/recover-stale` 先回收卡住状态。
5. 调用 `GET /api/migu/dictionaries` 初始化前端枚举和筛选项。
6. 调用 `GET /api/migu/tasks?page=1&page_size=20` 恢复历史批次列表。
7. 调用 `GET /api/migu/singers`、`GET /api/migu/albums`、`GET /api/migu/songs` 查看库内内容。
8. 先用 `GET /api/migu/settings` 查看当前并发、超时、重试和资源下载设置，需要调整时调用 `PUT/PATCH /api/migu/settings`。
9. 补池操作前先调用 `POST /api/migu/backfill/{type}/preview` 预览影响范围。
10. 资源队列治理前先调用 `POST /api/migu/resources/preview` 预览影响范围。
11. 批量处理资源前，前端可先调用 `POST /api/migu/pacmtoken/refresh` 传入当前 `pacmtoken`；长跑任务可轮询 `GET /api/migu/pacmtoken/state`，必要时调用 `POST /api/migu/pacmtoken/current` 同步本地 token。
12. 需要实际下载歌曲资源时调用 `POST /api/migu/resources/process`；同步接口建议先用较小 `limit` 实测，长时间任务使用 `async=true`。
13. 下载歌手头像或专辑封面时，先调用 `GET /api/migu/covers/queue?owner_type=singer` 或 `owner_type=album` 读取资源 ID，再调用 `POST /api/migu/covers/process`；成功后使用返回的 `cover.local_url` 读取本地图片，详见[本地封面下载](#歌手头像与专辑封面本地下载)。此流程读取已入库 URL，不需要歌曲 listen/pacmtoken。
14. 按业务阶段调用专用 crawl API、backfill retry API、资源治理 API 或资源处理 API。
15. 保存响应中的 `batch_id`，列表页可继续用 `GET /api/migu/tasks` 刷新持久化进度，并用 `GET /api/migu/tasks/summary` 展示全局批次指标。
16. 点开批次时调用 `GET /api/migu/tasks/{batch_id}`、`GET /api/migu/tasks/{batch_id}/items`、`GET /api/migu/tasks/{batch_id}/events`。
17. 元数据批次失败后按支持的类型使用 `rerun-failed`、`clone` 或 `backfill-retry` 创建新批次；封面失败项再次调用 `covers/process`，不原地重跑旧批次。
18. 需要内存级实时当前项时调用 `GET /api/migu/tasks/progress?batch_id=...`。
19. 需要停止时调用 `POST /api/migu/tasks/stop`。

`POST /api/migu/singers/crawl` 的 `limit=0` 表示每个分类全部获取。歌手目录主入口使用 `max_targets` 控制认领歌手数、`limit_per_singer` 控制每个歌手抓取数量，后者默认 `0` 表示在分页保护内全量获取，正数范围为 `1..500`。补池 retry 使用 `limit` 控制目标数、`limit_per_target` 控制每个歌手的抓取数量。

## 当前 API 清单

| API | 用途 | 数据与关键行为 |
| --- | --- | --- |
| `GET /api/health` | 健康检查 | 返回服务健康状态 |
| `GET /api/openapi.json` | 获取运行期接口契约 | 返回当前实际装配版本的 OpenAPI JSON |
| `POST /api/migu/singers/crawl` | 获取歌手分类列表 | `singers`、`singer_source_refs` |
| `POST /api/migu/singers/profile/refresh-from-db` | 独立补齐或刷新库内歌手整份资料 | 来源声明总数、有效资料及头像 URL，不改变目录生命周期 |
| `POST /api/migu/singer-songs/crawl-from-db` | 从库中认领未同步歌曲的歌手并批量获取歌曲 | `singer_source_refs.songs_sync_status`、歌曲来源表、批次表 |
| `POST /api/migu/singer-songs/crawl-from-db/preview` | 预览歌手歌曲目录采集范围 | 返回命中及可处理目标，不认领或采集 |
| `POST /api/migu/singer-albums/crawl-from-db` | 从库中认领未同步专辑的歌手并批量获取专辑 | `singer_source_refs.albums_sync_status`、专辑来源表、批次表 |
| `POST /api/migu/singer-albums/crawl-from-db/preview` | 预览歌手专辑目录采集范围 | 返回命中及可处理目标，不认领或采集 |
| `GET /api/migu/overview` | 控制台总览 | 从 `singers`、`songs`、`albums`、来源表和同步状态聚合 |
| `GET /api/migu/integrity` | 状态一致性检查 | 歌手缺歌曲/缺专辑按真实关联表判断，并返回 stale running 计数 |
| `GET /api/migu/schema/status` | Schema 检查 | 返回 MySQL schema 版本、缺失表和缺失关键列 |
| `POST /api/migu/maintenance/recover-stale` | 回收 stale running | 把超时停留在 `1 同步中` 的元数据/批次标为失败 |
| `GET /api/migu/singers` | 歌手列表 | 支持 `q` 模糊搜索、`source_singer_id` 精确 ID、`gender`、`country`、`songs_sync_status`、`albums_sync_status`、`min/max_songs_count`、`min/max_albums_count`；`q` 命中完整歌手 ID 时精确项优先；歌曲/专辑汇总按真实关联表派生，并返回 Migu 声明总数和剩余数 |
| `GET /api/migu/singers/{source_singer_id}` | 歌手详情 | 按 Migu 歌手 ID 查询，并返回真实歌曲/专辑汇总、Migu 声明总数、是否完成及本地 `cover` 信息 |
| `GET /api/migu/singers/{source_singer_id}/songs` | 歌手歌曲 | 查询该歌手的歌曲来源 |
| `GET /api/migu/singers/{source_singer_id}/albums` | 歌手专辑 | 查询该歌手的专辑来源及本地 `cover` 信息 |
| `GET /api/migu/albums` | 专辑列表 | 支持 `q`、`artist`、`kind`、`detail_sync_status`、`songs_sync_status`、`min/max_songs_count`；专辑歌曲数按 `album_song_refs` 实时统计 |
| `GET /api/migu/albums/{source_album_id}` | 专辑详情 | 按 Migu 专辑 ID 查询，并返回本地 `cover` 信息 |
| `GET /api/migu/albums/{source_album_id}/songs` | 专辑歌曲 | 查询该专辑的歌曲来源 |
| `GET /api/migu/songs` | 歌曲列表 | 支持 `q`、`artist`、`album`、`resource_status`、`format` |
| `GET /api/migu/songs/{source_song_id}` | 歌曲详情 | 按 Migu songId 或 contentId 查询 |
| `GET /api/migu/covers/queue` | 歌手/专辑封面队列 | 查询资源 ID、本地可用性和独立云端状态，支持 `cloud_status` 筛选 |
| `POST /api/migu/covers/process` | 歌手/专辑封面下载及云端存储 | 本地成功后，按开关自动上传百度或 Google 并核验回写；已下载项仅补传，默认后台批次 |
| `POST /api/migu/covers/recover-stale` | 封面超时恢复 | 恢复本地及已启用云端的超时认领，使旧代次失效并保留成功回执 |
| `GET /api/migu/covers/{owner_type}/{resource_id}/content` | 本地封面图片 | 按资源归属返回图片，支持 MIME、ETag/304；缺失或损坏返回 404 |
| `GET /api/migu/resources/queue` | 资源任务队列 | 查看资源任务，不下载资源 |
| `POST /api/migu/pacmtoken/refresh` | 运行期刷新 `pacmtoken` | 调用 Migu `checkPacMtoken`，成功后把 token 写入后端内存 |
| `GET /api/migu/pacmtoken/state` | 查询 token 脱敏状态 | 只返回长度和更新时间，不返回 token 明文 |
| `POST /api/migu/pacmtoken/current` | 获取当前内存 token | 仅供前端显式同步本地 token，不落库 |
| `POST /api/migu/resources/process` | 资源下载处理 | 认领待处理资源，获取临时 URL，下载保存并回写状态 |
| `POST /api/migu/resources/preview` | 资源治理预览 | 返回资源重试/重置/回收前的命中数量、可操作数量和样例 |
| `POST /api/migu/resources/retry` | 资源任务重试 | 将失败资源任务重新置为待获取，不执行下载；`4 skipped` 保持跳过 |
| `POST /api/migu/resources/reset` | 资源任务重置 | 重置资源任务状态、重试次数和错误信息，不删除文件 |
| `POST /api/migu/resources/recover-stale` | 资源任务超时恢复 | 将超时获取中的资源任务置为失败 |
| `GET /api/migu/dictionaries` | 前端字典 | 同步状态、性别、专辑类型、资源类型、音质、失败分类 |
| `GET /api/migu/settings` | 查询运行设置 | 返回当前设置及允许修改的字段 |
| `PUT /api/migu/settings` | 更新运行设置 | 校验并更新可变调度、并发及资源下载设置 |
| `PATCH /api/migu/settings` | 部分更新运行设置 | 与 PUT 共用字段校验及更新流程 |
| `GET /api/migu/backfill/*/queue` | 补池队列 | `*` 为 `singer-songs`、`singer-albums`、`album-details`、`album-songs` |
| `POST /api/migu/backfill/*/preview` | 补池预览 | 返回筛选命中数量、可操作数量、样例目标和不可操作原因 |
| `POST /api/migu/backfill/*/retry` | 补池重试 | 按 ID 或筛选条件启动批次 |
| `POST /api/migu/backfill/*/reset` | 补池重置 | 只重置同步状态，不启动任务 |
| `GET /api/migu/tasks` | 批次列表 | `crawl_batches`，默认隐藏归档批次，可传 `include_archived=true` |
| `GET /api/migu/tasks/progress` | 内存批次进度 | 通过 `batch_id` 查询当前任务快照 |
| `POST /api/migu/tasks/stop` | 请求停止批次 | 通过 `batch_id` 设置协作取消，并更新任务状态 |
| `GET /api/migu/tasks/summary` | 批次全局指标 | 状态分布、运行中数量、最近成功/失败批次、失败最多任务类型 |
| `GET /api/migu/tasks/{batch_id}` | 批次详情 | `crawl_batches` |
| `GET /api/migu/tasks/{batch_id}/items` | 批次目标列表 | `crawl_batch_items` |
| `GET /api/migu/tasks/{batch_id}/events` | 批次事件流 | `crawl_batch_events` |
| `POST /api/migu/tasks/{batch_id}/rerun-failed` | 重跑失败 item | 从旧批次失败/取消/超时 item 创建新批次 |
| `POST /api/migu/tasks/{batch_id}/clone` | 克隆批次 | 使用旧批次参数和目标创建新批次 |
| `POST /api/migu/tasks/{batch_id}/backfill-retry` | 从批次 item 回补池重试 | 从支持的歌手/专辑 item 生成补池 retry |
| `POST /api/migu/tasks/{batch_id}/archive` | 归档批次 | 只隐藏批次，不删除历史 |
| `GET /api/migu/governance/events` | 治理审计 | 查看资源、补池、维护和批次治理操作历史 |

清单按 HTTP 方法和路径列出全部 54 个注册业务操作；补池的 `*` 表示 `singer-songs`、`singer-albums`、`album-details`、`album-songs` 四种有效类型，展开后为 66 个有效操作。代码中的数字路径占位符与文档中的命名占位符等价。`/api/migu/*` 的 50 个 `OPTIONS` 注册用于统一 CORS 预检，不另列为业务操作；静态和运行期 OpenAPI 对通用补池参数与具体类型路径的表达不同，展开有效类型后覆盖一致。

## 内容查询

列表统一返回：

```json
{
  "items": [],
  "page": 1,
  "page_size": 20,
  "total": 0
}
```

常用示例：

```text
GET /api/migu/singers?q=周杰伦&country=华语&gender=1&page=1&page_size=20
GET /api/migu/singers?source_singer_id=112
GET /api/migu/singers?min_songs_count=1&max_albums_count=20
GET /api/migu/singers/112
GET /api/migu/singers/112/songs?page=1&page_size=50
GET /api/migu/singers/112/albums?detail_sync_status=0,3,4&min_album_songs_count=1

GET /api/migu/albums?q=太阳之子&artist=周杰伦&kind=2
GET /api/migu/albums?min_songs_count=1&max_songs_count=30
GET /api/migu/albums/600927015009001083
GET /api/migu/albums/600927015009001083/songs

GET /api/migu/songs?q=搁浅&artist=周杰伦&format=HQ
GET /api/migu/songs/1140736758
GET /api/migu/covers/queue?owner_type=singer&status=0,3&page_size=20
GET /api/migu/covers/queue?owner_type=album&status=0,3&page_size=20
POST /api/migu/covers/process
GET /api/migu/resources/queue?status=0,3&kind=audio&format=HQ
POST /api/migu/pacmtoken/refresh
GET /api/migu/pacmtoken/state
POST /api/migu/pacmtoken/current
POST /api/migu/resources/process
GET /api/migu/schema/status
```

状态字段约定：

- 同步状态：`0 未同步`、`1 同步中`、`2 已完成`、`3 失败`、`4 部分完成`。
- 资源状态：`0 待获取`、`1 获取中`、`2 已完成`、`3 失败`、`4 跳过`。
- 多状态过滤使用逗号分隔，例如 `status=0,3,4`。
- `1 同步中` 是临时运行态。服务启动会执行一次轻量 stale 恢复；前端也可以手动调用维护接口回收长时间卡住的 running 状态。
- 歌手和专辑详情会返回 `*_last_batch_id`、`*_running_batch_id`；补池队列统一返回 `last_batch_id`、`running_batch_id`、`last_processed_at`，前端可以直接跳转最近批次排障。
- `GET /api/migu/singers` 和 `GET /api/migu/singers/{source_singer_id}` 的歌手汇总继续统计真实关系：`songs_sync_count` / `songs_count` 来自 `song_artist_refs`，`albums_sync_count` / `albums_count` 来自 `album_artist_refs`。`songs_sync_status`、`albums_sync_status` 和同步时间只读取对应目录的采集结果，不再由关系数量推算。专辑响应可带入歌曲、歌曲响应可带入专辑，因此计数达到平台声明总数也不能证明目录已采全；`songs_completed`、`albums_completed` 只依状态判断。总数为 `null` 时不能确认全量完成。
- 歌手资料随专辑、歌曲目录采集统一补齐；声明总数未知时获取一份完整 `singer/info` 响应并随目录批次入库。需要刷新已知资料时，在主入口或歌手 backfill retry 中传 `force: true`，无需单独调用资料刷新接口。
- 前端展示可直接读取别名字段：歌手返回 `songs_count`/`albums_count` 和对应 `*_total`/`*_remaining_count`，专辑返回 `songs_count`/`album_songs_count`。这些字段不写入汇总表，真实计数由查询时实时聚合得到；数量过滤同样使用实时事实计数。
- 专辑 `songs_sync_count` 单独表示最近一次专辑歌曲采集成功处理条数，直接读取 `album_source_refs.songs_sync_count`；`album_songs_count` 和 `songs_count` 表示当前累计保存的去重关系数。歌手歌曲等入口也可以创建专辑关系，因此两种数量可以不同，专辑歌曲完成情况只按 `songs_sync_status` 判断。
- 三类来源接口的 `source_item_key` 是根据平台 ID 计算的兼容字段，不再持久化；专辑 `source_title` 是公共专辑 `title` 的兼容别名，数据库只保存一份标题。
- 数量过滤参数：歌手列表和歌手补池支持 `min_songs_count`、`max_songs_count`、`min_albums_count`、`max_albums_count`；专辑列表、歌手专辑列表和专辑补池支持 `min_songs_count`、`max_songs_count`，也兼容 `min_album_songs_count`、`max_album_songs_count`。
- 歌曲入库会同步维护 `song_artist_refs` 和 `album_song_refs`：`song_artist_refs` 只保存歌曲来源到歌手来源的关系，按歌手入口采集时会标记 `is_seed` 并关联 `singer_source_refs.id`，不再持久化 `source_artist_id/name` 这类歌手属性。`SongRecord.primary_artist_id/name` 从该关系表和歌手来源表派生，不再来自 `song_source_refs`；专辑歌曲查询会优先使用 `album_song_refs` 保存的 `source_album_id`、Disc、track 和平台顺序。
- `songs` 当前作为歌曲标题池使用，`normalized_title` 是唯一键；不同平台来源或不同专辑里的同名歌曲通过 `song_source_refs.song_db_id` 引用同一标题记录，平台时长、拼音、资源入口和版权 ID 保留在来源表。
- 歌曲资源任务会稳定生成 `cover`、`lyric`、`mrc`、`trc`；音频按 `audioFormats` 生成 `audio` 任务并保存 `resource_type`、`iformat`、`aformat`、`isize`、`asize`。如果平台未返回任何 `audioFormats`，会生成一个空 `format` 的 `audio` 待处理任务，方便前端识别“缺少音质参数”。

## Pacmtoken 运行期刷新

`POST /api/migu/pacmtoken/refresh`

用途：调用 Migu `checkPacMtoken` 校验/刷新当前 `pacmtoken`，并把成功结果写入后端进程内存，供前端定时器和后续 `POST /api/migu/resources/process` 使用。

请求：

```json
{
  "pacmtoken": "当前 pacmtoken",
  "store": true
}
```

也可以传完整 Cookie：

```json
{
  "cookie": "foo=1; pacmtoken=当前 pacmtoken; bar=2"
}
```

字段说明：

- `pacmtoken`：当前原始 token。也兼容误传完整 Cookie，但前端能拿到完整 Cookie 时建议放到 `cookie` 字段。
- `cookie`：可选，完整 Cookie 字符串；后端只提取其中的 `pacmtoken`。
- `store`：可选，默认 `true`；成功后写入后端进程内存。
- `update_runtime`：`store` 的兼容别名。

调用规则：

- 第一次调用必须传 `pacmtoken` 或包含 `pacmtoken` 的 `cookie`。
- 成功并写入内存后，后续可用空 body 调用，后端会使用内存中的 token 继续刷新。
- 后续 `resources/process` 获取 Migu listen 资源 URL 时只使用这个运行期 token；未设置时不会从 `.env` 或环境变量读取 Cookie。
- 资源批次启动前会尝试刷新一次；长跑过程中超过配置的 `pacmtoken_refresh_interval_seconds` 会自动刷新。遇到 `440013` 时可按配置刷新后额外重试一次。
- 该接口只调用 `checkPacMtoken`，不实现登录，不持久化 token，也不写入 `.env`。后端不会把 token 写入批次表、事件、日志或 OpenAPI 示例；前端不要记录响应里的 `pacmtoken` 明文日志。

响应：

```json
{
  "success": true,
  "refreshed": true,
  "pacmtoken": "新 pacmtoken",
  "pacmtoken_length": 56,
  "upstream_status": 200,
  "updated_at": 1779370000,
  "message": "migu pacmtoken refreshed"
}
```

状态码：

- `200`：刷新成功，或上游返回 token 未变化。
- `400`：请求体格式错误、token 过长、包含非法换行，或首次调用未提供 token。
- `502`：Migu `checkPacMtoken` 拒绝或上游响应不可用。
- `503`：后端未装配 pacmtoken 刷新服务。

脱敏状态：

`GET /api/migu/pacmtoken/state`

```json
{
  "configured": true,
  "has_pacmtoken": true,
  "pacmtoken_length": 56,
  "updated_at": 1779370000
}
```

当前内存 token：

`POST /api/migu/pacmtoken/current`

```json
{
  "configured": true,
  "has_pacmtoken": true,
  "pacmtoken_length": 56,
  "updated_at": 1779370000,
  "pacmtoken": "当前内存 pacmtoken"
}
```

`current` 只用于前端在长跑自动刷新后更新本地存储；不要把响应体写入日志。

## Schema 检查

`GET /api/migu/schema/status`

返回：

```json
{
  "source": "migu",
  "schema_version": 2026052109,
  "latest_applied_version": 2026052109,
  "latest_available_version": 2026052109,
  "schema_name": "song artist refs relationship only",
  "ok": true,
  "pending_migrations": [],
  "checksum_mismatches": [],
  "missing_tables": [],
  "missing_columns": [],
  "checked_at": 1779300000
}
```

当前正式入口是 `sql/mysql/migrations/`。服务启动时会自动应用未执行 migration；如果迁移失败或已执行版本 checksum 与文件不一致，MySQL schema 初始化失败，服务不可用。后续 DDL 变更必须新增 migration 文件。

可通过 `CRAWLER_MYSQL_MIGRATIONS_PATH` 指定 migration 目录，默认 `sql/mysql/migrations`。前端只根据 `ok=false`、`pending_migrations`、`checksum_mismatches`、`missing_tables` 或 `missing_columns` 提示环境异常，不应自行修改数据库。

错误响应统一包含稳定字段：

```json
{
  "error_code": "bad_request",
  "message": "page must be a positive integer",
  "error": "page must be a positive integer",
  "details": null
}
```

`error` 是兼容字段，新前端应优先读取 `error_code` 和 `message`。

## 补池治理

队列查看：

```text
GET /api/migu/backfill/singer-songs/queue?status=0,3,4&q=周杰伦
GET /api/migu/backfill/singer-albums/queue?country=华语&gender=1&min_albums_count=0
GET /api/migu/backfill/album-details/queue?artist=周杰伦&kind=2
GET /api/migu/backfill/album-songs/queue?status=3&min_album_songs_count=1
```

队列响应统一使用同一视图模型，不再需要根据 singer/album 字段推断：

歌手补池队列和歌手列表使用同一份目录采集状态。`sync_count` 保留关系事实统计：歌曲来自 `song_artist_refs`，专辑来自 `album_artist_refs`；默认 `status=0,3,4` 按保存的采集状态选择目标，不因关联数量达到声明总数而跳过。受 `limit` 截断的采集保持部分完成，即使数据库已有更多关联内容。`force=true` 可重采已完成目录，但不抢占运行中的任务。

补池队列的数量过滤同内容列表一致：歌手队列支持 `min_songs_count`、`max_songs_count`、`min_albums_count`、`max_albums_count`；专辑队列支持 `min_songs_count`、`max_songs_count`，也兼容 `min_album_songs_count`、`max_album_songs_count`。

普通 `albumId=1`“单曲发行”公共归档保留在内容列表，不进入 `album-details` 或 `album-songs` 回补队列。显式指定该 ID 时，预览、重试和重置以 `shared_release_archive` 说明跳过原因，`force` 不改变此规则。已有归档歌曲及关系保持原样；其他 ID 的同名真实专辑和有明确数字类型或物料身份的专辑仍完整采集曲目。

```json
{
  "items": [
    {
      "target_id": "112",
      "target_type": "singer",
      "target_name": "周杰伦",
      "owner_name": "",
      "sync_status": 3,
      "sync_count": 0,
      "last_error": "network timeout",
      "last_batch_id": "batch_1779200000000",
      "running_batch_id": null,
      "last_processed_at": 1779200000,
      "eligible_actions": ["retry", "reset"],
      "eligible": true,
      "reason": ""
    }
  ],
  "page": 1,
  "page_size": 20,
  "total": 1
}
```

补池操作前可以预览：

```text
POST /api/migu/backfill/singer-songs/preview
POST /api/migu/backfill/album-details/preview
```

```json
{
  "filter": {
    "status": [3],
    "q": "周杰伦"
  },
  "limit": 50
}
```

返回：

```json
{
  "type": "singer-songs",
  "source": "migu",
  "matched_count": 10,
  "eligible_count": 8,
  "ineligible_count": 2,
  "missing_count": 0,
  "sample_size": 20,
  "items": []
}
```

重试会创建新批次并返回 `batch_id`：

```json
{
  "source_ids": ["112"],
  "limit_per_target": 50
}
```

也可以按筛选条件重试：

```json
{
  "filter": {
    "status": [3],
    "artist": "周杰伦"
  },
  "limit": 20,
  "limit_per_target": 50
}
```

retry 使用数据库 claim 语义：后端会生成本次 `claim_token`，先把命中的目标原子置为 `1 同步中`，再只按同一个 `claim_token` 回查目标创建批次。连续或并发 retry 不会重复认领同一条可治理目标。已完成、不存在或当前状态不允许认领的显式 ID 会计入 `skipped_count`。

retry/reset 都会返回汇总和明细。显式 ID 场景下，`items` 会包含每个 ID 的处理结果；筛选批量场景下，reset 会返回最多 20 条样例：

```json
{
  "batch_id": "batch_1779200000000",
  "claimed_count": 8,
  "success_count": 8,
  "failed_count": 2,
  "skipped_count": 2,
  "items": [
    {"source_id": "112", "name": "周杰伦", "status": 1, "reason": "claimed"},
    {"source_id": "404", "name": "", "status": 0, "reason": "skipped"}
  ]
}
```

重置只更新同步状态，不启动任务。为避免误操作，必须提供 `source_ids` 或至少一个 `filter`：

```json
{
  "source_album_ids": ["600927015009001083"],
  "filter": {
    "status": [3]
  }
}
```

## 一致性检查

`GET /api/migu/integrity?stale_after_seconds=3600`

返回：

```json
{
  "singers_without_songs": 0,
  "singers_without_albums": 0,
  "albums_without_details": 0,
  "albums_without_songs": 0,
  "songs_without_resource_tasks": 0,
  "resources_failed": 0,
  "resources_running_stale": 0,
  "singers_songs_running_stale": 0,
  "singers_albums_running_stale": 0,
  "albums_details_running_stale": 0,
  "albums_songs_running_stale": 0,
  "running_batches_stale": 0,
  "generated_at": 1779200000
}
```

其中 `singers_without_songs` 统计没有任何 `song_artist_refs` 真实歌曲关联的歌手来源，`singers_without_albums` 统计没有任何 `album_artist_refs` 真实专辑关联的歌手来源。这两个字段不再只看 `songs_sync_status` 或 `albums_sync_status`，所以已有真实歌曲/专辑的歌手不会被误计入缺失项。

## Stale Running 恢复

`POST /api/migu/maintenance/recover-stale`

```json
{
  "source": "migu",
  "stale_after_seconds": 3600,
  "types": ["singer-songs", "singer-albums", "album-details", "album-songs", "resources", "batches"]
}
```

`types` 为空或省略时恢复全部支持类型。恢复行为只做状态治理：把超时停留在 `1 同步中` 的元数据和资源记录置为 `3 失败`，写入脱敏错误 `stale running recovered`，清空对应 claim 字段；超时运行批次会置为 `Failed`，未完成 item 置为 `Failed`，并追加 `timeoutRecovered` 事件。该接口不发起外部请求，不下载资源。

## 字典接口

`GET /api/migu/dictionaries`

返回：

- `sync_status`：`0 pending`、`1 running`、`2 completed`、`3 failed`、`4 partial`
- `gender`：`0 unknown`、`1 male`、`2 female`、`3 group`
- `album_kind`：`0 unknown`、`1 album`、`2 digital_album`
- `resource_kind`：`audio`、`cover`、`lyric`、`mrc`、`trc`
- `audio_format`：`PQ`、`HQ`、`SQ`、`ZQ24`、`Z3D`
- `error_category`：`network`、`parse`、`persistence`、`copyright`、`resource`、`invalid_target`、`timeout`、`unknown`
- `result_code`：`ok`、`network_failed`、`parse_failed`、`persist_failed`、`invalid_target`、`timeout`、`partial_completed`、`failed`

## 资源下载处理

`POST /api/migu/resources/process`

该接口是歌曲资源正式处理入口：从 MySQL `song_resources` 条件认领待处理记录，音频资源先换取本次临时 URL，再流式下载、校验并提交本地文件，最后按认领代次回写 `local_path`、大小、checksum、状态和批次结果。同步入口与异步入口复用同一编排，主流程不自动上传百度网盘，不持久化远端 URL。文件使用独立尝试路径，数据库确认前保留补偿与启动核对日志；客户端应使用返回的实际路径。完整字段归属、事务和恢复说明见 [持久化流程](../../architecture/persistence-flow.md)。

请求：

```json
{
  "source": "migu",
  "limit": 10,
  "offset": 0,
  "async": false,
  "batch_id": "resource_batch_1779200000000",
  "filter": {
    "kind": "audio",
    "format": "ZQ24",
    "content_id": "600902000006889266",
    "status": [0, 3]
  },
  "options": {
    "resource_download_concurrency": 3,
    "resource_download_retries": 1,
    "resource_download_timeout_ms": 120000,
    "pacmtoken_refresh_interval_seconds": 300,
    "auto_refresh_pacmtoken": true,
    "refresh_pacmtoken_on_440013": true
  }
}
```

字段说明：

- `limit`：本次最多认领资源数，建议同步调用先使用 `1..10`，最大 `1000`。
- `offset`：队列偏移，默认 `0`。
- `async`：可选，`true` 时立即返回 `202 + batch_id` 并后台处理，适合长时间资源下载；默认 `false` 保持同步响应。
- `batch_id`：可选；省略时服务端生成。
- `filter.kind`：资源类型，常用 `audio`、`lyric`/`lrc`、`mrc`、`trc`、`cover`；别名支持 `resource_kind`、`resourceKind`。
- `filter.format`：音质档位，例如 `PQ`、`HQ`、`SQ`、`ZQ24`、`Z3D`；别名支持 `format_type`、`formatType`。
- `filter.content_id`：Migu contentId；别名支持 `contentId`、`source_content_id`、`sourceContentId`。
- `filter.status`：资源状态数组；别名支持 `resource_status`、`resourceStatus`、`statuses`。
- `options.resource_download_concurrency`：本批次资源下载并发，范围 `1..32`，默认来自 settings，当前默认 `3`。
- `options.resource_download_retries`：可恢复下载错误重试次数，范围 `0..5`，默认 `1`。
- `options.resource_download_timeout_ms`：单次资源内容下载超时，范围 `10000..600000`，默认 `120000`。
- `options.pacmtoken_refresh_interval_seconds`：长跑自动刷新 token 间隔，范围 `0..3600`，`0` 表示关闭定时刷新，默认 `300`。
- `options.auto_refresh_pacmtoken`：是否在批次启动和运行中自动刷新 token，默认 `true`。
- `options.refresh_pacmtoken_on_440013`：遇到 `440013` 是否刷新 token 后额外重试一次，默认 `true`。
- `options.process_related_resources`：是否处理 listen 响应带出的 `cover/lyric/mrc/trc` 关联资源，默认 `true`。

处理规则：

- `audio` 会使用歌曲来源参数中的 `contentId`、`resourceType`、`copyrightId` 和 `format` 调用 Migu listen 接口换取 `url`，然后下载该 URL；调用前需要先通过 `POST /api/migu/pacmtoken/refresh` 写入运行期 `pacmtoken`。
- 主资源在批次内部按 `resource_download_concurrency` 有限并发处理；实际并行度还受服务启动参数 `CRAWLER_RESOURCE_WORKER_THREADS` 限制。
- 只对 `network_failed`、`timeout`、HTTP `5xx`、listen URL 获取网络失败这类可恢复错误重试。音频每次重试都会重新调用 listen 获取新的临时 URL；数字专辑、`440016`、参数缺失、解析结构错误和持久化失败不重试。
- 第一条成功返回的音频响应会带出 `cover/lyric/mrc/trc`，同一首歌的关联资源只下载一次，避免每个音质重复下载。
- 响应 `items` 会包含本次主资源以及由音频响应带出的 `cover/lyric/mrc/trc` 关联资源明细；如果关联 URL 存在但队列中没有可认领任务，会返回 `resource_id=0`、`result_code=related_not_claimed` 的跳过明细。
- 处理开始后会写入统一批次表，`task_type=resourceProcess`，前端可通过 `/api/migu/tasks`、`/api/migu/tasks/{batch_id}/items`、`/api/migu/tasks/{batch_id}/events` 查看结果。同步和异步模式都会实时写入 item 开始、完成、失败事件；`pacmtokenRefreshed` 事件只记录长度和时间，不记录 token。
- 已知数字专辑会在请求 Migu URL 前直接跳过；如果 Migu 返回 `440016`，也会把同一首歌的相关音频资源标记为跳过，避免重复浪费请求。`440013` 这类疑似登录态或账号权限问题保留为失败资源，换 token 后可 retry。
- 本地相对路径固定为 `migu/songs/{contentId}/audio/{format}.{ext}`、`migu/songs/{contentId}/lyrics/lyric.lrc`、`migu/songs/{contentId}/lyrics/mrc.mrc`、`migu/songs/{contentId}/lyrics/trc.trc`。
- 当前 filesystem 生产装配没有歌曲下载后的自动 uploader，`CRAWLER_RESOURCE_CLOUD_UPLOAD_ENABLED` 只接入歌手/专辑封面。歌曲结果中的 `cloud_backend/cloud_path` 保留兼容字段，下面的新下载示例为空字符串；历史队列的非空云端回执也不表示本次下载进行了上传。歌曲手动归档与核对见[运行说明](../../operations/resource-storage.md)。旧 `RESOURCE_STORAGE_BACKEND=rclone` 是另一条远端 copyurl 存储路径，区别见[配置说明](../../configuration.md#存储模式与封面云端)。

同步响应：

```json
{
  "batch_id": "resource_batch_1779200000000",
  "claimed_count": 1,
  "succeeded_count": 1,
  "failed_count": 0,
  "skipped_count": 0,
  "related_succeeded_count": 2,
  "related_failed_count": 0,
  "related_skipped_count": 0,
  "synthetic_related_skipped_count": 0,
  "items": [
    {
      "resource_id": 123,
      "song_source_ref_id": 456,
      "source": "migu",
      "source_song_id": "2643",
      "source_content_id": "600902000006889266",
      "title": "夜曲",
      "resource_kind": "audio",
      "format_type": "ZQ24",
      "ok": true,
      "skipped": false,
      "related": false,
      "synthetic": false,
      "local_path": "migu/songs/600902000006889266/audio/ZQ24.flac",
      "cloud_backend": "",
      "cloud_path": "",
      "size_bytes": 16266000,
      "checksum": "sha256...",
      "error_category": "",
      "result_code": "ok",
      "message": "stored"
    },
    {
      "resource_id": 124,
      "song_source_ref_id": 456,
      "source": "migu",
      "source_song_id": "2643",
      "source_content_id": "600902000006889266",
      "title": "夜曲",
      "resource_kind": "lyric",
      "format_type": "",
      "ok": true,
      "skipped": false,
      "related": true,
      "synthetic": false,
      "local_path": "migu/songs/600902000006889266/lyrics/lyric.lrc",
      "cloud_backend": "",
      "cloud_path": "",
      "size_bytes": 1024,
      "checksum": "sha256...",
      "error_category": "",
      "result_code": "ok",
      "message": "stored"
    }
  ]
}
```

异步响应：

```http
HTTP/1.1 202 Accepted
```

```json
{
  "accepted": true,
  "async": true,
  "batch_id": "resource_batch_1779200000000",
  "task_url": "/api/migu/tasks/resource_batch_1779200000000",
  "items_url": "/api/migu/tasks/resource_batch_1779200000000/items",
  "events_url": "/api/migu/tasks/resource_batch_1779200000000/events"
}
```

## 资源队列治理边界

资源队列治理只处理 `song_resources` 的队列状态，不解析真实资源 URL，不下载资源，不写本地文件。

`POST /api/migu/resources/preview`

```json
{
  "source": "migu",
  "action": "retry",
  "resource_ids": [123],
  "filter": {
    "status": [3],
    "kind": "audio",
    "format": "HQ",
    "q": "周杰伦"
  },
  "limit": 50,
  "stale_after_seconds": 3600
}
```

返回：

```json
{
  "source": "migu",
  "action": "retry",
  "matched_count": 1,
  "eligible_count": 1,
  "ineligible_count": 0,
  "missing_count": 0,
  "items": [
    {
      "resource_id": 123,
      "target_id": "1140736758",
      "target_name": "搁浅",
      "resource_kind": "audio",
      "format_type": "HQ",
      "sync_status": 3,
      "eligible": true,
      "reason": "eligible_for_retry"
    }
  ]
}
```

preview 不写库、不写审计事件。`retry` 默认可操作 `3 failed`，`reset` 默认可操作 `0/1/3`，`recover-stale` 只可操作超过 `stale_after_seconds` 的 `1 running`。`4 skipped` 表示版权或数字专辑等永久跳过状态，不会被资源治理和下载入口重新排队。

`POST /api/migu/resources/retry`

```json
{
  "source": "migu",
  "resource_ids": [123],
  "filter": {
    "status": [3],
    "kind": "audio",
    "format": "HQ",
    "q": "周杰伦"
  },
  "limit": 50
}
```

`retry` 默认只处理 `3 failed`，把目标置为 `0 pending` 并清空 `last_error`。`reset` 默认处理可恢复状态 `0/1/3`，额外清零 `retry_count`。`recover-stale` 只处理超过 `stale_after_seconds` 的 `1 running` 资源任务，并置为 `3 failed`。即使请求显式传入 `status: [4]`，已跳过资源也不会被重新置为待获取。

资源队列和治理响应会稳定返回 `error_category`、`result_code`、`retry_count`、`last_batch_id`、`running_batch_id`、`last_processed_at` 和 `fetched_at`，便于前端按错误类型筛选和跳转最近批次。实际治理动作会写入 `governance_events`；preview 不写审计。

响应统一返回：

```json
{
  "action": "retry",
  "updated_count": 1,
  "success_count": 1,
  "failed_count": 0,
  "skipped_count": 0,
  "items": [
    { "resource_id": 123, "source_id": "1140736758", "name": "搁浅", "status": 0, "reason": "retry" }
  ]
}
```

## 歌手分类

`POST /api/migu/singers/crawl`

```json
{
  "tabs": [
    "huayu-nan",
    "huayu-nv",
    "huayu-group"
  ],
  "limit": 100,
  "start_index": 0,
  "end_index": 3
}
```

支持单个 tab：

```json
{
  "tab": "huayu-nan",
  "limit": 0
}
```

`limit=0` 表示该分类解析到的歌手全部入库；正数按传入数量截断，不再限制为 `1..500`。

支持 tab：

```text
huayu-nan, huayu-nv, huayu-group,
oumei-nan, oumei-nv, oumei-group,
rihan-nan, rihan-nv, rihan-group
```

## 歌手歌曲


`POST /api/migu/singer-songs/crawl-from-db`

```json
{
  "source": "migu",
  "filter": {
    "sync_status": [0, 3, 4],
    "country": "",
    "gender": null,
    "q": ""
  },
  "max_targets": 100,
  "limit_per_singer": 0
}
```

该接口从 `singer_source_refs` 中认领 `songs_sync_status` 命中的歌手，把状态先置为 `1 同步中`，再为每个歌手生成批次 item。`country`、`gender`、`q` 为空或省略时不参与过滤；`sync_status` 为空或省略时默认使用 `[0, 3, 4]`；`limit_per_singer=0` 表示按分页保护全量获取。

## 歌手专辑


`POST /api/migu/singer-albums/crawl-from-db`

```json
{
  "source": "migu",
  "filter": {
    "sync_status": [0, 3, 4],
    "country": "华语",
    "gender": 1,
    "q": "周杰伦"
  },
  "max_targets": 100,
  "limit_per_singer": 0
}
```

该接口从 `singer_source_refs` 中认领 `albums_sync_status` 命中的歌手。筛选字段都可省略；如果只想重试失败项，可以传 `"sync_status": [3]`；`limit_per_singer=0` 表示按分页保护全量获取。

## 显式重采与失败补齐

正常采集顺序为歌手索引、歌手专辑、歌手歌曲。专辑和歌曲目录采集会携带歌手资料和当前选择范围的专辑详情，不要求逐字段另发请求。仅需恢复失败或强制重采时使用现有 backfill。

主入口及其 `/preview` 支持 `force`，默认 `false`。`true` 将已完成歌手纳入候选，并为每个选中歌手刷新一次完整资料；后续歌曲、专辑仍使用同一批次写入。普通模式复用已知资料，避免重复请求。

```http
POST /api/migu/singer-albums/crawl-from-db
```

```json
{"source":"migu","max_targets":20,"limit_per_singer":0,"force":true}
```

按准确平台 ID 选择歌手，使用 backfill 的 `source_ids`；`limit_per_target` 控制每个歌手抓取数量。

```http
POST /api/migu/backfill/singer-songs/retry
```

```json
{"source":"migu","source_ids":["112"],"limit_per_target":20,"force":true}
```

单独刷新专辑详情使用 `POST /api/migu/backfill/album-details/retry`；补齐完整专辑曲目使用 `POST /api/migu/backfill/album-songs/retry`，保留完整分页和原子写入。两者都支持 `source_ids`、`limit`、`force`。专辑曲目认领仍要求详情已完成，`force` 不跳过此条件。主入口的 `force` 刷新歌手资料，不强制重取已完成的专辑详情。

`force` 不抢占 `1 同步中` 的任务。主入口已有的 `include_stale_running` 仅用于明确回收超时任务；backfill 通过维护接口先恢复超时状态。`/preview` 与 retry 使用相同强制重采资格；`reset` 拒绝 `force: true`，因为 reset 只负责重置状态。

独立补齐歌手资料使用 `POST /api/migu/singers/profile/refresh-from-db`：

```json
{
  "source": "migu",
  "limit": 50,
  "force": false,
  "filter": {}
}
```

- 默认选择 `source_song_total` 或 `source_album_total` 为 NULL 的来源；数值 0 表示已知，不重复补查。`limit=0` 选择全部匹配来源，正数范围为 1..500，默认 50。
- 可传 `source_ids` 数组精确选择平台歌手 ID（1..500 个，不使用本地主键）；重复 ID 去重，`limit` 限制实际选择数量。`filter` 支持 `q/country/gender`，默认不过滤未知分类。显式 ID 选择以 ID 为准。仅接受上述规范字段，未知字段或显式空 ID 数组返回 400，避免拼错参数后意外扩大选取范围。
- `force=true` 重新获取已知资料。一次资料响应同时保存有效名称、两个声明总数、拼音和头像 URL；空字段不覆盖已知值，未知类型和地区不推断。资料有效但仍缺可选字段不等于可以编造填充。
- 已核验的平台默认歌手头像按缺失处理，不登记为新增有效头像，也不覆盖已有地址；索引、资料及歌曲/专辑带入的歌手共用此规则。历史默认头像仍应作为真实头像缺口单独核查。
- 资料批次采用启动时 `settings.max_retries` 的有限重试，范围 0..10，0 表示禁用。网络故障、HTTP 5xx、临时无效 JSON 或空实体响应可重取；HTTP 429 重试前至少等待 20 秒。身份错配、明确业务拒绝、其他 HTTP 4xx、解析器异常及写入错误不自动重试。等待期间响应取消和超时。
- 每个目标的 `retry_count` 记录实际追加尝试，`itemRetryScheduled` 事件记录重试上下文；中途失败不增加最终失败计数，重试成功会清除旧错误。其他批次继续使用各自原有策略。
- 只请求整份歌手资料，不采集歌曲或专辑目录，不下载头像。目录同步状态、完成数量、时间及正在执行任务的认领信息保持不变。
- 有候选时返回 202 与 `batch_id/claimed_count/skipped_count/items`；没有候选时返回 200、`batch_id=null`。失败从任务明细查看，可再次按失败来源 ID 调用本接口。HTTP 错误、业务错误、身份错配和写入失败均不得记录为成功。
- 歌手查询的 `gender` 来自 `singer_source_refs.source_gender`；删除无业务作用的公共 `status` 返回字段。数据库不再保存 `singers.gender/singers.status/singer_source_refs.metadata_status`，目录状态及资源状态不受影响。

下列四条旧目录路径及其 OPTIONS 已移除，调用方需迁移：

| 原路径 | 替代路径 |
|---|---|
| `/api/migu/singer-songs/crawl` | `/api/migu/singer-songs/crawl-from-db`；精确 ID 使用 `backfill/singer-songs/retry` |
| `/api/migu/singer-albums/crawl` | `/api/migu/singer-albums/crawl-from-db`；精确 ID 使用 `backfill/singer-albums/retry` |
| `/api/migu/album-details/crawl` | `/api/migu/backfill/album-details/retry` |
| `/api/migu/album-songs/crawl` | `/api/migu/backfill/album-songs/retry` |

内部执行方法保留，已有批次仍可查询和重放。

## 任务响应

歌手索引和两个目录主入口启动后返回 `202` 和 `batch_id`；空队列也生成已完成的零目标批次。backfill retry 使用其治理响应，返回 `batch_id`、`claimed_count`、`skipped_count` 和目标结果，详见“补池治理”。

```json
{
  "batch_id": "batch_1779200000000",
  "message": "migu singer songs crawl started",
  "target_count": 2,
  "limit": 20,
  "start_index": 0,
  "end_index": 2
}
```

## 控制台总览

`GET /api/migu/overview`

```json
{
  "singers": 1200,
  "singer_sources": 1200,
  "songs": 50000,
  "song_sources": 50000,
  "albums": 3000,
  "album_sources": 3000,
  "singer_song_relations": 80000,
  "singer_album_relations": 3200,
  "album_song_relations": 42000,
  "singer_song_sync": {"0": 100, "2": 900, "3": 20},
  "singer_album_sync": {"0": 120, "2": 880},
  "album_detail_sync": {"0": 300, "1": 5, "2": 2500, "3": 195},
  "album_song_sync": {"0": 400, "2": 2300, "3": 300},
  "resource_status": {"0": 48000, "2": 1500, "3": 500}
}
```

同步状态含义：`0` 未同步，`1` 同步中，`2` 已完成，`3` 失败，`4` 部分完成。资源状态含义：`0` 待获取，`1` 获取中，`2` 已完成，`3` 失败，`4` 跳过。

`singer_song_relations`、`singer_album_relations`、`album_song_relations` 是实时关系行数，分别来自 `song_artist_refs`、`album_artist_refs` 和 `album_song_refs`，用于总览判断“已有多少事实关联”，不作为写入时强维护的汇总字段。

## 批次列表

`GET /api/migu/tasks?page=1&page_size=20&state=Running&type=albumDetails&q=周杰伦&display_name=周杰伦&target_summary=timeout&target_keywords=singer:112&last_error=network&error_category=network&result_code=network_failed&include_archived=false`

`q` 会覆盖批次 ID、任务类型、`display_name`、`target_summary`、`target_keywords`、批次/目标 `last_error`、目标 `error_category` 和目标 `result_code`，用于跨分页搜索歌手、专辑、目标名和失败原因。`error_category` 与 `result_code` 是目标级精确筛选，其余目标字段是模糊筛选。

```json
{
  "items": [
    {
      "batch_id": "batch_1779200000000_0",
      "source": "migu",
      "type": "albumDetails",
      "state": "Running",
      "target_count": 50,
      "processed": 20,
      "success": 18,
      "failed": 2,
      "display_name": "周杰伦",
      "target_summary": "周杰伦 112 error:network timeout",
      "target_keywords": "singer:112 singerSongs 周杰伦 network network_failed",
      "last_error": "network timeout",
      "error_category": "network",
      "result_code": "network_failed",
      "created_at": 1779200000,
      "started_at": 1779200001,
      "finished_at": null,
      "duration_seconds": 120
    }
  ],
  "page": 1,
  "page_size": 20,
  "total": 128
}
```

默认不返回已归档批次；排障时可传 `include_archived=true` 查看历史归档。

全局批次摘要：

`GET /api/migu/tasks/summary?stale_after_seconds=3600`

```json
{
  "state_distribution": [{"key": "Running", "count": 2}],
  "failure_task_types": [{"key": "albumDetails", "count": 5}],
  "latest_success": null,
  "latest_failed": null,
  "running_count": 2,
  "oldest_stale_running_at": 1779200000,
  "generated_at": 1779300000
}
```

## 批次详情

```text
GET /api/migu/tasks/{batch_id}
GET /api/migu/tasks/{batch_id}/items?page=1&page_size=50&state=Failed&error_category=network&result_code=network_failed
GET /api/migu/tasks/{batch_id}/events?page=1&page_size=100
```

`items` 用于展示每个歌手、分类 tab 或专辑的处理结果，`events` 用于展示启动、目标开始、目标完成、失败、取消、超时和批次结束事件。

## 批次二次治理

批次二次治理全部创建新批次，旧批次只读保留，避免原地重跑导致审计和并发语义混乱。

```text
POST /api/migu/tasks/{batch_id}/rerun-failed
POST /api/migu/tasks/{batch_id}/clone
POST /api/migu/tasks/{batch_id}/backfill-retry
POST /api/migu/tasks/{batch_id}/archive
```

请求示例：

```json
{
  "mode": "failed",
  "state": ["Failed", "Cancelled"],
  "error_category": "network",
  "result_code": "network_failed",
  "limit": 50,
  "limit_per_target": 50,
  "reason": "手动重试网络失败项"
}
```

治理响应：

```json
{
  "batch_id": "batch_1779300000000",
  "claimed_count": 10,
  "success_count": 10,
  "failed_count": 0,
  "skipped_count": 2,
  "items": [
    {"source_id": "112", "name": "周杰伦", "status": "claimed", "reason": "cloned"}
  ]
}
```

`rerun-failed` 从旧批次失败/取消/超时 item 创建新批次；`clone` 支持 `mode=all` 或 `mode=failed`；`backfill-retry` 当前只支持 `singerSongs`、`singerAlbums`、`albumDetail`、`albumSongs` 四类元数据 item；`archive` 只标记隐藏，不删除批次、item 或 event。

治理审计：

```text
GET /api/migu/governance/events?page=1&page_size=20&target_type=batch&batch_id=batch_1779300000000
```

审计事件覆盖资源 retry/reset/recover-stale、补池 retry/reset、maintenance recover-stale、批次 rerun/clone/backfill-retry/archive。返回字段包含 `action`、`target_type`、`target_id`、`batch_id`、`before_status`、`after_status`、`result_code`、`error_category`、`reason` 和 `payload`。

治理事件保留上述字段，同时新增直接可展示的目标快照字段：

| 字段 | 含义 |
| --- | --- |
| `target_name` | 主展示名，例如歌曲名、歌手名、专辑名或批次名。 |
| `target_summary` | 次级说明，例如 `音频 · PQ · 李荣浩`、`歌手歌曲补池`、`后台运行 · 50 条`。 |
| `target_source_id` | 平台对象 ID，例如 `source_content_id`、`source_song_id`、`source_album_id`、`source_singer_id`。 |
| `target_owner_name` | 归属展示名，资源优先为歌手名，专辑为艺人名。 |

新事件会在写入 `governance_events` 时保存这些快照；旧事件没有快照时，列表查询会按 `target_type` 尝试从资源、歌手、专辑或批次表回填。目标已不存在或无法匹配时，事件仍正常返回，新增字段为 `null`。成功的 `resource.retry`、`resource.reset` 不再把 `retry`、`reset` 这类重复动作写入 `reason`；失败、跳过或用户输入的真实原因仍保留。

返回示例：

```json
{
  "items": [
    {
      "event_id": 94406,
      "source": "migu",
      "action": "resource.retry",
      "target_type": "resource",
      "target_id": "94406",
      "target_name": "老友记",
      "target_summary": "音频 · PQ · 李荣浩",
      "target_source_id": "60091900000834282",
      "target_owner_name": "李荣浩",
      "batch_id": null,
      "before_status": "3",
      "after_status": "0",
      "result_code": "ok",
      "error_category": null,
      "reason": null,
      "payload": {},
      "created_at": 1779300000000
    }
  ],
  "page": 1,
  "page_size": 20,
  "total": 1
}
```

## 查询进度

`GET /api/migu/tasks/progress?batch_id=<batch_id>`

```json
{
  "batch_id": "batch_1779200000000",
  "state": "Running",
  "total": 2,
  "processed": 1,
  "success": 1,
  "failed": 0,
  "current_item": "singer:266",
  "recent_errors": [],
  "duration_seconds": 12,
  "percent": 50
}
```

`state` 取值：`Pending`、`Running`、`Completed`、`Failed`、`Cancelled`。

## 停止任务

`POST /api/migu/tasks/stop`

```json
{
  "batch_id": "batch_1779200000000"
}
```

停止是协作式的：已进入网络请求、解析或写库步骤的单个 target 会在当前步骤结束后退出。

## 并发设置

查询：

`GET /api/migu/settings`

更新：

`PUT /api/migu/settings`
`PATCH /api/migu/settings`

```json
{
  "max_batch_concurrency": 4,
  "max_page_concurrency": 8,
  "max_retries": 2,
  "task_timeout_ms": 600000,
  "resource_download_concurrency": 3,
  "resource_download_retries": 1,
  "resource_download_timeout_ms": 120000,
  "pacmtoken_refresh_interval_seconds": 300,
  "auto_refresh_pacmtoken": true,
  "refresh_pacmtoken_on_440013": true
}
```

字段含义：

| 字段 | 范围 | 含义 |
| --- | --- | --- |
| `max_batch_concurrency` | `1..128` | 一个任务内同时处理多少个 target。 |
| `max_page_concurrency` | `1..128` | 单个 target 内分页或详情窗口最大并发。 |
| `max_retries` | `0..10` | 每个 target 失败后的重试次数。 |
| `task_timeout_ms` | `1000..86400000` | 批量任务整体超时时间。 |
| `resource_download_concurrency` | `1..32` | 单个资源处理批次内部下载并发。 |
| `resource_download_retries` | `0..5` | 资源下载可恢复错误重试次数。 |
| `resource_download_timeout_ms` | `10000..600000` | 单次资源内容下载超时。 |
| `pacmtoken_refresh_interval_seconds` | `0..3600` | 资源长跑自动刷新 token 间隔，`0` 表示关闭定时刷新。 |
| `auto_refresh_pacmtoken` | `true/false` | 批次启动和运行中是否自动刷新 token。 |
| `refresh_pacmtoken_on_440013` | `true/false` | 遇到 `440013` 是否刷新 token 后额外重试一次。 |

这些字段支持 camelCase 别名：`maxBatchConcurrency`、`maxPageConcurrency`、`maxRetries`、`taskTimeoutMs`、`resourceDownloadConcurrency`、`resourceDownloadRetries`、`resourceDownloadTimeoutMs`、`pacmtokenRefreshIntervalSeconds`、`autoRefreshPacmtoken`、`refreshPacmtokenOn440013`。

`readonly.resource_worker_threads` 来自启动参数 `CRAWLER_RESOURCE_WORKER_THREADS`，默认 `8`；前端设置的资源并发可以高于 worker 数，实际任务会在 worker pool 中排队。


### 目录采集时统一补齐元数据

标准顺序为歌手索引入库、歌手专辑采集、歌手歌曲采集。专辑和歌曲入口会复用已知歌手总数；总数未知时按歌手获取一份完整资料，随本次实体和关系原子保存，不要求前端逐字段调用刷新接口。专辑来源尚未采集详情时，按来源身份获取一次完整详情，携带简介、声明曲目数、日期及可用拼音；后续目录采集复用已保存状态和有界响应缓存，不因平台仍缺少某一字段而循环补查。

专辑阶段保存目标专辑返回的全部曲目，按平台来源身份去重，补齐已有字段和专辑、歌手关系；曲目不足声明总数时复用详情首段，仅补必要分页。歌曲携带的专辑、歌手信息同样随批次建档或补缺。`limit` 控制入口选取的专辑或歌曲数量，不再过滤目标专辑已经返回的曲目；关联实体落库不会触发其其他目录或推荐专辑的递归采集。详情与必要缺页复用同一在途请求和有界缓存，重启后复用数据库已核实的详情、曲目状态。专辑查询同时返回 `source_title_pinyin`、`summary` 和 `cover_url`，歌手查询返回 `avatar_url`。平台未提供的字段保持未知；曲目分页失败时保留已返回数据，并记录该专辑的部分完成状态。

普通 `albumId=1`“单曲发行”按公共归档处理：只保存当前响应携带的歌曲、摘要和关系，常规采集不自动展开其详情和全部曲目，也不从缓存或旧库完整归档中补入其他歌曲。真实专辑仍完整入库，数字专辑、其他平台及其他 ID 的同名专辑不受此规则影响。接口参数和响应字段不变；历史归档数据不会被自动清理。

### 元数据与二进制下载分离

歌手索引、歌手资料、专辑列表和专辑详情响应中的有效图片地址，分别保存到 `singer_resources.remote_url`、`album_resources.remote_url`，与本批实体、关系在同一事务提交。新资源只登记为 `status=0`，不生成本地文件，也不把已完成资源重置为待下载。空地址及已核验的默认歌手头像不清除已有地址，列表缩略图不覆盖已采集详情的封面地址。

元数据采集完成后不再自动下载歌手头像、专辑封面、歌曲封面或 `lrc/mrc/trc`。歌曲资源任务仍正常生成，音频、歌曲封面及歌词继续由现有 `POST /api/migu/resources/process` 获取，歌曲远端地址仍由 listen 提供，不新增歌曲 URL 持久字段。歌手和专辑封面使用独立的 `/api/migu/covers` 路由族处理，见下方“歌手头像与专辑封面本地下载”。

## 歌手头像与专辑封面本地下载

元数据采集继续只保存图片 URL。此独立流程读取 `singer_resources.remote_url` 或 `album_resources.remote_url` 下载到本地，当前生产装配支持 MySQL。开启云端后，同一 process 自动上传百度或 Google 并核验回写，详情见[云端存储](../../roadmap/metadata-cover-cloud-storage.md)。图片来源与本地认领边界见[资源说明](../../migu/resources.md#歌手头像与专辑封面本地下载)。

| 接口 | 作用 |
| --- | --- |
| `GET /api/migu/covers/queue` | 查询指定所有者类型的图片状态及本地可用性 |
| `POST /api/migu/covers/process` | 下载库中 URL，默认创建后台批次；指定 `async=false` 可顺序处理小批次 |
| `POST /api/migu/covers/recover-stale` | 恢复超时认领并使旧代次失效 |
| `GET /api/migu/covers/{owner_type}/{resource_id}/content` | 按资源归属返回已提交图片，支持 ETag/304；文件缺失或损坏返回 404 |

`owner_type` 必须为 `singer` 或 `album`；`resource_id` 属于对应资源表，不是名称池 ID 或平台 ID。队列 query 支持 `source=migu`、逗号分隔的 `resource_ids/status/cloud_status` 以及 `page/page_size`，每页最多 200 项。参数格式、类型或范围不合法返回 400；未配置或持久化不可用返回 503。

下载示例：

```json
{
  "owner_type": "singer",
  "source": "migu",
  "limit": 20,
  "async": true,
  "concurrency": 3,
  "retries": 1,
  "timeout_ms": 15000
}
```

关闭云端时默认处理本地待获取 `0` 与失败 `3`，认领后为 `1`，完整文件和数据库回执确认后为 `2`。开启云端后，还会选择本地 `2` 且缺失、失败、待核对或目标已变化的云端回执，直接补传已有完整文件；无需 `refresh`。`resource_ids` 最多 200 项；`limit` 为 `1..200`，后台 `concurrency` 为 `1..32`，额外重试 `retries` 为 `0..5`，本地单图超时为 `100..600000` 毫秒，大小上限为 8 MiB。请求只接受声明字段，不接受任意 URL 或文件路径。

后台返回 `202` 和 `batch_id`，复用既有进度、任务明细、事件与停止接口。任务类型为 `metadataCovers`，明细类型为 `singerCover/albumCover`，payload 包含 `owner_type/resource_id/source_ref_id`，不包含远端图片 URL。同步返回 `200`、`items` 及 `success_count/failed_count/skipped_count`，单项失败由明细表达。

对已完成封面换图或修复本地丢失文件时，必须显式指定资源：

```json
{
  "owner_type": "album",
  "resource_ids": [1, 2],
  "refresh": true,
  "async": true
}
```

刷新期间及失败后保留旧成功文件，队列状态和 `available` 分别表达任务进展与图片是否可用。请求开始后 URL 已更新或代次已改变时，旧下载不能覆盖新结果；提交不确定返回 `writeback_uncertain`，新文件及日志保留待核对。失败项可直接再次 process，无需先 reset。

超时恢复示例：`{"owner_type":"album","stale_after_seconds":1800}`。阈值范围 `1..604800` 秒，使用独立认领时间；普通元数据更新时间不延长认领。恢复后待处理/失败项可以重新下载，已有图片继续可用。

歌手和专辑列表、详情增加可空的 `cover` 对象，包含 `owner_type/resource_id/source_ref_id/status/available/local_url/local_path/size_bytes/checksum` 及脱敏错误。`local_url` 在已提交文件存在时返回内容路由，否则为 null。原 `avatar_url/cover_url` 保持平台远端地址含义，`local_path` 是模块相对路径，绝对存储根不返回。

本地内容接口按实际图片返回 MIME 和 SHA-256 ETag，缓存策略要求重新验证；`If-None-Match` 匹配时返回 304。两类所有者中相同的数字资源 ID 始终分别查询，不能跨所有者取图。

HTTPS 默认校验证书链及主机名，Windows 额外只读加载系统 ROOT 信任库。平台可能返回与 URL 后缀不同的 WebP 等格式，保存后缀和内容 MIME 均按实际字节识别，HTTP adapter 保留该 MIME。

云端配置：`CRAWLER_RESOURCE_CLOUD_UPLOAD_ENABLED=true`；后端 `CRAWLER_RESOURCE_CLOUD_UPLOAD_BACKEND=baidupcs` 使用已登录的 BaiduPCS-Go，`rclone` 使用现有 RC 与 Google Drive 命名 remote。百度路径为 `{CRAWLER_RESOURCE_CLOUD_ROOT}/{local_path}`；Google 路径为 `{CRAWLER_RCLONE_TARGET_FS}/{local_path}`，例如 `gcloud:Media/migu/albums/...`。云端上传及核验总超时由 `CRAWLER_RESOURCE_CLOUD_TIMEOUT_MS` 控制，默认 120000 ms。

响应的 `cloud` 对象包含 `enabled/status/current/backend/path/size_bytes/checksum/uploaded_at` 及脱敏错误。云端状态独立为 `0 待上传、1 运行中、2 已确认、3 失败、4 待核对`。`current=true` 表示回执匹配当前文件及已启用的目标配置。百度回读完整文件比对 SHA-256；Google RC 核对远端大小与完整 SHA-256 或 MD5。只有核验和条件 DB 回写均成功才把 item 计为成功。

云端失败时本地 `status=2`、文件和 `local_url` 保留，item 的 `success=false`、顶层错误与 `cloud` 错误描述云端阶段。再次 process 仅补云端。云端 `path` 保存最后一次确认的对象，刷新或后端切换时可能暂时过期，应同时检查 `current`；该路径不是公开图片 URL。`cloud_status` 请求数组最多 5 项，值域 `0..4`。stale 恢复开启云端时合计本地和云端恢复数；两类认领代次独立。
