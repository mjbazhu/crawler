UPDATE album_source_refs
SET detail_sync_status = 0,
    detail_sync_error = NULL,
    detail_claim_token = '',
    detail_claimed_at = 0,
    detail_claim_owner = '',
    updated_at = UNIX_TIMESTAMP()
WHERE NOT (album_source_refs.source='migu' AND album_source_refs.source_album_id='1'
       AND COALESCE(album_source_refs.source_resource_type,'')<>'5' AND album_source_refs.source_album_kind<>2
       AND EXISTS (SELECT 1 FROM albums archive_title WHERE archive_title.id=album_source_refs.album_db_id AND archive_title.title IN ('','单曲发行'))
       AND NOT EXISTS (SELECT 1 FROM migu_dalbum_details archive_material WHERE archive_material.album_source_ref_id=album_source_refs.id AND archive_material.source_material_id<>'' AND archive_material.source_material_id<>album_source_refs.source_album_id))
  AND source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (? = '' OR source_album_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND detail_sync_status IN (?, ?, ?, ?, ?)
  AND (? = '' OR EXISTS (SELECT 1 FROM albums title_row WHERE title_row.id=album_source_refs.album_db_id AND title_row.title LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%')) OR source_album_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%'))
  AND (? = '' OR EXISTS (SELECT 1 FROM album_artist_refs aa JOIN singer_source_refs sr ON sr.id=aa.singer_source_ref_id JOIN singers sn ON sn.id=sr.singer_db_id WHERE aa.album_source_ref_id=album_source_refs.id AND (sn.name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR sr.source_singer_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)))
  AND (? = -1 OR source_album_kind = ?)
ORDER BY updated_at ASC, id ASC
LIMIT ?
