SELECT COUNT(*)
FROM singer_source_refs r
JOIN singers s ON s.id = r.singer_db_id
WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (? = '' OR s.name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR r.source_singer_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%'))
  AND (? = '' OR r.source_country = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = -1 OR s.gender = ?)
  AND (? = 0 OR r.songs_sync_status IN (?, ?, ?, ?, ?))
  AND (? = 0 OR r.albums_sync_status IN (?, ?, ?, ?, ?))
