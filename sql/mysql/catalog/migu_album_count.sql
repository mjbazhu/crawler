SELECT COUNT(*)
FROM album_source_refs r
JOIN albums a ON a.id = r.album_db_id
WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (? = '' OR r.source_title LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR r.source_album_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%'))
  AND (? = '' OR r.source_artist_name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR r.source_artist_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci OR a.primary_artist_name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%'))
  AND (? = -1 OR r.source_album_kind = ?)
  AND (? = 0 OR r.detail_sync_status IN (?, ?, ?, ?, ?))
  AND (? = 0 OR r.songs_sync_status IN (?, ?, ?, ?, ?))
  AND (? = -1 OR (SELECT COUNT(DISTINCT abr.song_source_ref_id) FROM album_song_refs abr WHERE abr.source = r.source AND abr.source_album_id = r.source_album_id) >= ?)
  AND (? = -1 OR (SELECT COUNT(DISTINCT abr.song_source_ref_id) FROM album_song_refs abr WHERE abr.source = r.source AND abr.source_album_id = r.source_album_id) <= ?)
