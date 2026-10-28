SELECT d.source_material_id, a.source_album_id
FROM JSON_TABLE(CAST(? AS JSON), '$[*]' COLUMNS (
    material_id VARCHAR(64) COLLATE utf8mb4_unicode_ci PATH '$'
)) AS input_ids
JOIN migu_dalbum_details d ON d.source_material_id = input_ids.material_id
JOIN album_source_refs a ON a.id = d.album_source_ref_id
WHERE a.source = 'migu'
ORDER BY d.source_material_id, a.source_album_id
FOR UPDATE OF d;
