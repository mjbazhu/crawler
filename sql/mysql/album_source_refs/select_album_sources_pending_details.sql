SELECT
    id,
    album_db_id,
    source,
    source_item_key,
    source_album_id,
    source_resource_type,
    source_res_type,
    '' AS source_action,
    source_album_kind,
    source_title,
    source_artist_id,
    source_artist_name,
    '' AS cover_url,
    detail_sync_status,
    detail_synced_at,
    detail_sync_error,
    updated_at,
    COALESCE((SELECT d.source_material_id FROM migu_dalbum_details d WHERE d.album_source_ref_id=pending.id),'')
FROM album_catalog_sources pending
WHERE NOT (pending.source='migu' AND pending.source_album_id='1'
       AND COALESCE(pending.source_resource_type,'')<>'5' AND pending.source_album_kind<>2
       AND EXISTS (SELECT 1 FROM albums archive_title WHERE archive_title.id=pending.album_db_id AND archive_title.title IN ('','单曲发行'))
       AND NOT EXISTS (SELECT 1 FROM migu_dalbum_details archive_material WHERE archive_material.album_source_ref_id=pending.id AND archive_material.source_material_id<>'' AND archive_material.source_material_id<>pending.source_album_id))
  AND source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND detail_sync_status IN (0, 3, 4)
ORDER BY updated_at ASC, id ASC
LIMIT ? OFFSET ?
