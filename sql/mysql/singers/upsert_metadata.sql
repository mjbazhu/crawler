INSERT INTO singers (
    name,
    normalized_name,
    created_at,
    updated_at
) VALUES (
    ?,
    ?,
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
) ON DUPLICATE KEY UPDATE
    id = LAST_INSERT_ID(id),
    name = COALESCE(NULLIF(name, ''), VALUES(name)),
    updated_at = VALUES(updated_at);
