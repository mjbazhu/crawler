SELECT COUNT(*)
FROM song_resources sr
STRAIGHT_JOIN song_source_refs r ON r.id = sr.song_source_ref_id
WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (? = '' OR EXISTS (SELECT 1 FROM songs st WHERE st.id=r.song_db_id AND st.title LIKE CONCAT('%',CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,'%')) OR r.source_song_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR r.source_content_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%'))
  AND (? = 0 OR sr.status IN (?, ?, ?, ?, ?))
  AND (? = '' OR sr.resource_kind = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = '' OR sr.format_type = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
