SELECT
    COUNT(*) AS matched_count,
    COUNT(CASE
        WHEN (
            r.songs_sync_status IN (?, ?, ?, ?, ?)
            OR (? = 1 AND r.songs_sync_status = 1 AND r.updated_at <= UNIX_TIMESTAMP() - ?)
        ) THEN 1
    END) AS eligible_count
FROM singer_source_refs r
JOIN singers s ON s.id = r.singer_db_id
WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (? = '' OR r.source_country = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = -1 OR s.gender = ?)
  AND (
      ? = ''
      OR s.name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%')
      OR r.source_singer_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%')
  )
