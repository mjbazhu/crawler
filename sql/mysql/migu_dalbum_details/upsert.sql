INSERT INTO migu_dalbum_details (
    album_source_ref_id,
    source_material_id,
    created_at,
    updated_at
) VALUES (
    ?,
    NULLIF(?, ''),
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
) ON DUPLICATE KEY UPDATE
    id = LAST_INSERT_ID(id),
    source_material_id = COALESCE(VALUES(source_material_id), source_material_id),
    updated_at = VALUES(updated_at);
