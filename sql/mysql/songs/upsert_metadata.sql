INSERT INTO songs (
    title,
    normalized_title,
    isrc,
    created_at,
    updated_at
) VALUES (
    ?,
    ?,
    NULL,
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
) ON DUPLICATE KEY UPDATE
    id = LAST_INSERT_ID(id),
    title = VALUES(title),
    updated_at = VALUES(updated_at);
