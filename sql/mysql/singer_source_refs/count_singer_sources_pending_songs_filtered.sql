SELECT
    COUNT(*) AS matched_count,
    COUNT(CASE
        WHEN (
            (summary.sync_status <> 1 AND summary.sync_status IN (?, ?, ?, ?, ?))
            OR (? = 1 AND summary.sync_status = 1 AND summary.updated_at <= UNIX_TIMESTAMP() - ?)
        ) THEN 1
    END) AS eligible_count
FROM (
    SELECT
        r.source_singer_id,
        s.name,
        COALESCE(r.source_country, '') AS source_country,
        r.source_gender AS gender,
        r.songs_sync_status AS sync_status,
        r.updated_at
    FROM singer_source_refs r
    JOIN singers s ON s.id = r.singer_db_id
    WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
) summary
WHERE (? = '' OR summary.source_country = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = -1 OR summary.gender = ?)
  AND (
      ? = ''
      OR summary.name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%')
      OR summary.source_singer_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%')
  )
