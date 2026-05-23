# Migu 元数据 API 对接说明

当前 HTTP API 只保留本阶段已经落库的 Migu 元数据链路和歌曲资源处理链路：歌手、歌手歌曲、歌手专辑、专辑详情、专辑歌曲、歌曲音频和歌词资源。

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
13. 按业务阶段调用专用 crawl API、backfill retry API、资源治理 API 或资源处理 API。
14. 保存响应中的 `batch_id`，列表页可继续用 `GET /api/migu/tasks` 刷新持久化进度，并用 `GET /api/migu/tasks/summary` 展示全局批次指标。
15. 点开批次时调用 `GET /api/migu/tasks/{batch_id}`、`GET /api/migu/tasks/{batch_id}/items`、`GET /api/migu/tasks/{batch_id}/events`。
16. 批次失败后优先用 `rerun-failed`、`clone` 或 `backfill-retry` 创建新批次，不原地重跑旧批次。
17. 需要内存级实时当前项时调用 `GET /api/migu/tasks/progress?batch_id=...`。
18. 需要停止时调用 `POST /api/migu/tasks/stop`。

`limit` 表示每个手动目标或每次专辑补齐最多处理的数量，范围 `1..500`。从库歌手批量接口使用 `max_targets` 控制认领歌手数，使用 `limit_per_singer` 控制每个歌手最多抓取数量。

## 当前 API 清单

| API | 用途 | 入库重点 |
| --- | --- | --- |
| `POST /api/migu/singers/crawl` | 获取歌手分类列表 | `singers`、`singer_source_refs` |
| `POST /api/migu/singer-songs/crawl` | 根据歌手 ID 获取歌曲 | `songs`、`song_source_refs`、`song_artist_refs`、`album_song_refs`、`song_source_audio_formats`、`song_resources` |
| `POST /api/migu/singer-songs/crawl-from-db` | 从库中认领未同步歌曲的歌手并批量获取歌曲 | `singer_source_refs.songs_sync_status`、歌曲来源表、批次表 |
| `POST /api/migu/singer-albums/crawl` | 根据歌手 ID 获取专辑列表 | `albums`、`album_source_refs` |
| `POST /api/migu/singer-albums/crawl-from-db` | 从库中认领未同步专辑的歌手并批量获取专辑 | `singer_source_refs.albums_sync_status`、专辑来源表、批次表 |
| `POST /api/migu/album-details/crawl` | 补齐待处理专辑详情 | `albums`、`album_source_refs`、`migu_dalbum_details`，并解析响应里的 `songItems` |
| `POST /api/migu/album-songs/crawl` | 单独补齐已同步详情专辑的歌曲 | `songs`、`song_source_refs`、`album_song_refs`、`song_artist_refs`、`song_source_audio_formats`、`song_resources` |
| `GET /api/migu/overview` | 控制台总览 | 从 `singers`、`songs`、`albums`、来源表和同步状态聚合 |
| `GET /api/migu/integrity` | 状态一致性检查 | 返回未完成歌手/专辑/资源任务计数和 stale running 计数 |
| `GET /api/migu/schema/status` | Schema 检查 | 返回 MySQL schema 版本、缺失表和缺失关键列 |
| `POST /api/migu/maintenance/recover-stale` | 回收 stale running | 把超时停留在 `1 同步中` 的元数据/批次标为失败 |
| `GET /api/migu/singers` | 歌手列表 | 支持 `q`、`gender`、`country`、`songs_sync_status`、`albums_sync_status` |
| `GET /api/migu/singers/{source_singer_id}` | 歌手详情 | 按 Migu 歌手 ID 查询 |
| `GET /api/migu/singers/{source_singer_id}/songs` | 歌手歌曲 | 查询该歌手的歌曲来源 |
| `GET /api/migu/singers/{source_singer_id}/albums` | 歌手专辑 | 查询该歌手的专辑来源 |
| `GET /api/migu/albums` | 专辑列表 | 支持 `q`、`artist`、`kind`、`detail_sync_status`、`songs_sync_status` |
| `GET /api/migu/albums/{source_album_id}` | 专辑详情 | 按 Migu 专辑 ID 查询 |
| `GET /api/migu/albums/{source_album_id}/songs` | 专辑歌曲 | 查询该专辑的歌曲来源 |
| `GET /api/migu/songs` | 歌曲列表 | 支持 `q`、`artist`、`album`、`resource_status`、`format` |
| `GET /api/migu/songs/{source_song_id}` | 歌曲详情 | 按 Migu songId 或 contentId 查询 |
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
| `GET /api/migu/backfill/*/queue` | 补池队列 | `*` 为 `singer-songs`、`singer-albums`、`album-details`、`album-songs` |
| `POST /api/migu/backfill/*/preview` | 补池预览 | 返回筛选命中数量、可操作数量、样例目标和不可操作原因 |
| `POST /api/migu/backfill/*/retry` | 补池重试 | 按 ID 或筛选条件启动批次 |
| `POST /api/migu/backfill/*/reset` | 补池重置 | 只重置同步状态，不启动任务 |
| `GET /api/migu/tasks` | 批次列表 | `crawl_batches`，默认隐藏归档批次，可传 `include_archived=true` |
| `GET /api/migu/tasks/summary` | 批次全局指标 | 状态分布、运行中数量、最近成功/失败批次、失败最多任务类型 |
| `GET /api/migu/tasks/{batch_id}` | 批次详情 | `crawl_batches` |
| `GET /api/migu/tasks/{batch_id}/items` | 批次目标列表 | `crawl_batch_items` |
| `GET /api/migu/tasks/{batch_id}/events` | 批次事件流 | `crawl_batch_events` |
| `POST /api/migu/tasks/{batch_id}/rerun-failed` | 重跑失败 item | 从旧批次失败/取消/超时 item 创建新批次 |
| `POST /api/migu/tasks/{batch_id}/clone` | 克隆批次 | 使用旧批次参数和目标创建新批次 |
| `POST /api/migu/tasks/{batch_id}/backfill-retry` | 从批次 item 回补池重试 | 从支持的歌手/专辑 item 生成补池 retry |
| `POST /api/migu/tasks/{batch_id}/archive` | 归档批次 | 只隐藏批次，不删除历史 |
| `GET /api/migu/governance/events` | 治理审计 | 查看资源、补池、维护和批次治理操作历史 |

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
GET /api/migu/singers/112
GET /api/migu/singers/112/songs?page=1&page_size=50
GET /api/migu/singers/112/albums?detail_sync_status=0,3,4

GET /api/migu/albums?q=太阳之子&artist=周杰伦&kind=2
GET /api/migu/albums/600927015009001083
GET /api/migu/albums/600927015009001083/songs

GET /api/migu/songs?q=搁浅&artist=周杰伦&format=HQ
GET /api/migu/songs/1140736758
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
- 歌曲入库会同步维护 `song_artist_refs` 和 `album_song_refs`：多歌手查询通过 `song_artist_refs`，按歌手入口采集时会标记 `is_seed` 并尽量关联 `singer_source_refs.id`；`SongRecord.primary_artist_id/name` 也从该关系表派生，不再来自 `song_source_refs`；专辑歌曲查询会优先使用 `album_song_refs` 保存的 `source_album_id`、Disc、track 和平台顺序。
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
  "schema_version": 2026052107,
  "latest_applied_version": 2026052107,
  "latest_available_version": 2026052107,
  "schema_name": "song artist singer ref and song source cleanup",
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
GET /api/migu/backfill/singer-albums/queue?country=华语&gender=1
GET /api/migu/backfill/album-details/queue?artist=周杰伦&kind=2
GET /api/migu/backfill/album-songs/queue?status=3
```

队列响应统一使用同一视图模型，不再需要根据 singer/album 字段推断：

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

该接口是歌曲资源正式处理入口：从 MySQL `song_resources` 队列认领待处理记录，音频资源先调用 Migu 资源接口换取临时 URL，再下载内容、写入存储并回写 `remote_url`、`local_path`、大小、状态和批次结果。当前存储实现是本地文件系统，后续切换 rclone/API 存储时只替换资源内容存储 adapter，不改变 API 入参和响应结构。

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
- `filter.kind`：资源类型，常用 `audio`、`lyric`、`mrc`、`trc`、`cover`；别名支持 `resource_kind`、`resourceKind`。
- `filter.format`：音质档位，例如 `PQ`、`HQ`、`SQ`、`ZQ24`、`Z3D`；别名支持 `format_type`、`formatType`。
- `filter.content_id`：Migu contentId；别名支持 `contentId`、`source_content_id`、`sourceContentId`。
- `filter.status`：资源状态数组；别名支持 `resource_status`、`resourceStatus`、`statuses`。
- `options.resource_download_concurrency`：本批次资源下载并发，范围 `1..32`，默认来自 settings，当前默认 `3`。
- `options.resource_download_retries`：可恢复下载错误重试次数，范围 `0..5`，默认 `1`。
- `options.resource_download_timeout_ms`：单次资源内容下载超时，范围 `10000..600000`，默认 `120000`。
- `options.pacmtoken_refresh_interval_seconds`：长跑自动刷新 token 间隔，范围 `0..3600`，`0` 表示关闭定时刷新，默认 `300`。
- `options.auto_refresh_pacmtoken`：是否在批次启动和运行中自动刷新 token，默认 `true`。
- `options.refresh_pacmtoken_on_440013`：遇到 `440013` 是否刷新 token 后额外重试一次，默认 `true`。

处理规则：

- `audio` 会使用歌曲来源参数中的 `contentId`、`resourceType`、`copyrightId` 和 `format` 调用 Migu listen 接口换取 `url`，然后下载该 URL；调用前需要先通过 `POST /api/migu/pacmtoken/refresh` 写入运行期 `pacmtoken`。
- 主资源在批次内部按 `resource_download_concurrency` 有限并发处理；实际并行度还受服务启动参数 `CRAWLER_RESOURCE_WORKER_THREADS` 限制。
- 只对 `network_failed`、`timeout`、HTTP `5xx`、listen URL 获取网络失败这类可恢复错误重试。音频每次重试都会重新调用 listen 获取新的临时 URL；数字专辑、`440016`、参数缺失、解析结构错误和持久化失败不重试。
- 第一条成功返回的音频响应会带出 `lrcUrl`、`mrcUrl`、`trcUrl`，同一首歌的歌词类资源只下载一次，避免每个音质重复下载。
- 响应 `items` 会包含本次主资源以及由音频响应带出的 `lyric/mrc/trc` 关联资源明细；如果关联 URL 存在但队列中没有可认领任务，会返回 `resource_id=0`、`result_code=related_not_claimed` 的跳过明细。
- 处理开始后会写入统一批次表，`task_type=resourceProcess`，前端可通过 `/api/migu/tasks`、`/api/migu/tasks/{batch_id}/items`、`/api/migu/tasks/{batch_id}/events` 查看结果。同步和异步模式都会实时写入 item 开始、完成、失败事件；`pacmtokenRefreshed` 事件只记录长度和时间，不记录 token。
- 已知数字专辑会在请求 Migu URL 前直接跳过；如果 Migu 返回 `440016`，也会把同一首歌的相关音频资源标记为跳过，避免重复浪费请求。`440013` 这类疑似登录态或账号权限问题保留为失败资源，换 token 后可 retry。
- 本地相对路径固定为 `migu/songs/{contentId}/audio/{format}.{ext}`、`migu/songs/{contentId}/lyrics/lyric.lrc`、`migu/songs/{contentId}/lyrics/mrc.mrc`、`migu/songs/{contentId}/lyrics/trc.trc`。

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
  "limit": 100
}
```

支持 tab：

```text
huayu-nan, huayu-nv, huayu-group,
oumei-nan, oumei-nv, oumei-group,
rihan-nan, rihan-nv, rihan-group
```

## 歌手歌曲

`POST /api/migu/singer-songs/crawl`

```json
{
  "singer_ids": [112, 266],
  "limit": 50,
  "start_index": 0,
  "end_index": 2
}
```

支持单个歌手：

```json
{
  "singer_id": 112,
  "limit": 50
}
```

### 从库批量获取歌手歌曲

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
  "limit_per_singer": 50
}
```

该接口从 `singer_source_refs` 中认领 `songs_sync_status` 命中的歌手，把状态先置为 `1 同步中`，再为每个歌手生成批次 item。`country`、`gender`、`q` 为空或省略时不参与过滤；`sync_status` 为空或省略时默认使用 `[0, 3, 4]`。

## 歌手专辑

`POST /api/migu/singer-albums/crawl`

```json
{
  "singer_ids": [112, 266],
  "limit": 50
}
```

该接口只获取歌手专辑列表，并把待补齐详情状态写入 `album_source_refs`。详情和专辑歌曲由下面两个补齐接口触发。

### 从库批量获取歌手专辑

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
  "limit_per_singer": 50
}
```

该接口从 `singer_source_refs` 中认领 `albums_sync_status` 命中的歌手。筛选字段都可省略；如果只想重试失败项，可以传 `"sync_status": [3]`。

## 专辑详情补齐

`POST /api/migu/album-details/crawl`

```json
{
  "source": "migu",
  "limit": 50
}
```

该接口从 `album_source_refs` 中选择 `detail_sync_status IN (0, 3, 4)` 的记录，获取详情接口并保存详情字段。详情响应中如果包含 `songItems`，会同步解析并写入歌曲来源，同时更新 `songs_sync_status`。

## 专辑歌曲补齐

`POST /api/migu/album-songs/crawl`

```json
{
  "source": "migu",
  "limit": 50
}
```

该接口从 `album_source_refs` 中选择 `detail_sync_status = 2` 且 `songs_sync_status IN (0, 3, 4)` 的记录，重新获取专辑详情响应并只解析 `songItems`。

## 任务响应

所有启动接口都返回 `202`：

```json
{
  "batch_id": "batch_1779200000000",
  "message": "migu singer songs crawl started",
  "target_count": 2,
  "limit": 50,
  "start_index": 0,
  "end_index": 2
}
```

专辑补齐接口还会返回内部目标：

```json
{
  "batch_id": "batch_1779200000000",
  "message": "migu album details backfill started",
  "target_count": 50,
  "limit": 50,
  "start_index": 0,
  "end_index": 50
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
  "singer_song_sync": {"0": 100, "2": 900, "3": 20},
  "singer_album_sync": {"0": 120, "2": 880},
  "album_detail_sync": {"0": 300, "1": 5, "2": 2500, "3": 195},
  "album_song_sync": {"0": 400, "2": 2300, "3": 300},
  "resource_status": {"0": 48000, "2": 1500, "3": 500}
}
```

同步状态含义：`0` 未同步，`1` 同步中，`2` 已完成，`3` 失败，`4` 部分完成。资源状态含义：`0` 待获取，`1` 获取中，`2` 已完成，`3` 失败，`4` 跳过。

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
