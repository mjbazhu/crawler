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
    r.songs_sync_status,
    r.songs_synced_at,
    r.songs_sync_count,
    COALESCE(r.songs_sync_error, ''),
    r.updated_at
FROM singer_source_refs r
JOIN singers s ON s.id = r.singer_db_id
WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (
      r.songs_sync_status IN (?, ?, ?, ?, ?)
      OR (? = 1 AND r.songs_sync_status = 1 AND r.updated_at <= UNIX_TIMESTAMP() - ?)
  )
  AND (? = '' OR r.source_country = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = -1 OR s.gender = ?)
  AND (
      ? = ''
      OR s.name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%')
      OR r.source_singer_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%')
  )
ORDER BY r.updated_at ASC, r.id ASC
LIMIT ? OFFSET ?
