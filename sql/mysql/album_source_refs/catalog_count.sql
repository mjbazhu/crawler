SELECT COUNT(*)
FROM album_source_refs r
WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (? = '' OR EXISTS (SELECT 1 FROM albums title_row WHERE title_row.id=r.album_db_id AND title_row.title LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%')) OR r.source_album_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%'))
  AND (? = '' OR EXISTS (SELECT 1 FROM album_artist_refs aa JOIN singer_source_refs sr ON sr.id=aa.singer_source_ref_id JOIN singers sn ON sn.id=sr.singer_db_id WHERE aa.album_source_ref_id=r.id AND (sn.name LIKE CONCAT('%',CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,'%') OR sr.source_singer_id=CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci OR sn.name LIKE CONCAT('%',CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,'%'))))
  AND (? = -1 OR r.source_album_kind = ?)
  AND (? = 0 OR NOT (r.source='migu' AND r.source_album_id='1'
       AND COALESCE(r.source_resource_type,'')<>'5' AND r.source_album_kind<>2
       AND EXISTS (SELECT 1 FROM albums archive_title WHERE archive_title.id=r.album_db_id AND archive_title.title IN ('','单曲发行'))
       AND NOT EXISTS (SELECT 1 FROM migu_dalbum_details archive_material WHERE archive_material.album_source_ref_id=r.id AND archive_material.source_material_id<>'' AND archive_material.source_material_id<>r.source_album_id)))
  AND (? = 0 OR r.detail_sync_status IN (?, ?, ?, ?, ?))
  AND (? = 0 OR r.songs_sync_status IN (?, ?, ?, ?, ?))
  AND (? = -1 OR (SELECT COUNT(*) FROM album_song_refs abr WHERE abr.album_source_ref_id=r.id) >= ?)
  AND (? = -1 OR (SELECT COUNT(*) FROM album_song_refs abr WHERE abr.album_source_ref_id=r.id) <= ?)
