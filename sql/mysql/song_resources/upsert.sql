INSERT INTO song_resources (
    song_source_ref_id,
    resource_kind,
    format_type,
    status,
    created_at,
    updated_at
) VALUES (
    ?,
    ?,
    ?,
    0,
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
) ON DUPLICATE KEY UPDATE
    id = LAST_INSERT_ID(id),
    status = IF(status IN (2, 4), status, 0),
    updated_at = VALUES(updated_at);
