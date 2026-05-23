SELECT
    r.id,
    r.singer_db_id,
    r.source,
    r.source_item_key,
    r.source_singer_id,
    s.name,
    COALESCE(r.source_country, ''),
    s.gender,
    COALESCE(r.avatar_url, ''),
    r.albums_sync_status,
    r.albums_synced_at,
    r.albums_sync_count,
    COALESCE(r.albums_sync_error, ''),
    r.updated_at
FROM singer_source_refs r
JOIN singers s ON s.id = r.singer_db_id
WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND r.albums_sync_status IN (0, 3, 4)
ORDER BY r.updated_at ASC, r.id ASC
LIMIT ? OFFSET ?
