SELECT
    r.source,
    CONCAT('album:',r.source_album_id) AS source_item_key,
    r.source_album_id,
    COALESCE(r.source_resource_type, ''),
    COALESCE(r.source_res_type, ''),
    '',
    r.source_album_kind,
    COALESCE(r.source_title, ''),
    COALESCE(r.source_artist_id, ''),
    COALESCE(r.source_artist_name, ''),
    '',
    COALESCE(r.total_count, ''),
    r.detail_sync_status,
    a.title,
    a.normalized_title,
    a.primary_artist_name,
    COALESCE((SELECT d.source_material_id FROM migu_dalbum_details d WHERE d.album_source_ref_id=r.id),'')
FROM album_catalog_sources r
JOIN album_catalog a ON a.id = r.album_db_id
WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND r.source_album_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
LIMIT 1
