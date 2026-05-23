SELECT
    id,
    album_db_id,
    source,
    source_item_key,
    source_album_id,
    source_resource_type,
    source_res_type,
    source_action,
    source_album_kind,
    source_title,
    source_artist_id,
    source_artist_name,
    cover_url,
    total_count,
    detail_sync_status,
    songs_sync_status,
    songs_synced_at,
    songs_sync_count,
    songs_sync_error,
    updated_at
FROM album_source_refs
WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND detail_sync_status = 2
  AND songs_sync_status IN (0, 3, 4)
ORDER BY updated_at ASC, id ASC
LIMIT ? OFFSET ?
