SELECT COUNT(*)
FROM album_source_refs r
WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (? = '' OR EXISTS (SELECT 1 FROM albums title_row WHERE title_row.id=r.album_db_id AND title_row.title LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%')) OR r.source_album_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%'))
  AND (? = '' OR EXISTS (SELECT 1 FROM album_artist_refs aa JOIN singer_source_refs sr ON sr.id=aa.singer_source_ref_id JOIN singers sn ON sn.id=sr.singer_db_id WHERE aa.album_source_ref_id=r.id AND (sn.name LIKE CONCAT('%',CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,'%') OR sr.source_singer_id=CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci OR sn.name LIKE CONCAT('%',CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,'%'))))
  AND (? = -1 OR r.source_album_kind = ?)
  AND (? = 0 OR r.detail_sync_status IN (?, ?, ?, ?, ?))
  AND (? = 0 OR r.songs_sync_status IN (?, ?, ?, ?, ?))
  AND (? = -1 OR (SELECT COUNT(*) FROM album_song_refs abr WHERE abr.album_source_ref_id=r.id) >= ?)
  AND (? = -1 OR (SELECT COUNT(*) FROM album_song_refs abr WHERE abr.album_source_ref_id=r.id) <= ?)
