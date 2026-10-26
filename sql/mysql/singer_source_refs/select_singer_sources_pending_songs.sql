SELECT
    r.id,
    r.singer_db_id,
    r.source,
    CONCAT('singer:',r.source_singer_id) AS source_item_key,
    r.source_singer_id,
    s.name,
    COALESCE(r.source_country, ''),
    s.gender,
    '',
    r.songs_sync_status,
    r.songs_synced_at,
    r.songs_sync_count,
    COALESCE(r.songs_sync_error, ''),
    r.updated_at
FROM singer_source_refs r
JOIN singers s ON s.id = r.singer_db_id
WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND r.songs_sync_status IN (0, 3, 4)
ORDER BY r.updated_at ASC, r.id ASC
LIMIT ? OFFSET ?
