SELECT
    r.id,
    r.singer_db_id,
    r.source,
    r.source_item_key,
    r.source_singer_id,
    r.source_view_id,
    r.source_res_type,
    r.source_action,
    r.source_track,
    s.name,
    r.source_display_id,
    r.source_name_pinyin,
    r.source_tag,
    r.source_metric,
    r.source_country,
    s.gender,
    r.avatar_url,
    r.metadata_status,
    r.songs_sync_status,
    r.songs_synced_at,
    r.songs_sync_count,
    r.songs_sync_error,
    r.albums_sync_status,
    r.albums_synced_at,
    r.albums_sync_count,
    r.albums_sync_error,
    r.created_at,
    r.updated_at
FROM singer_source_refs r
JOIN singers s ON s.id = r.singer_db_id
WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND r.source_singer_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
LIMIT 1
