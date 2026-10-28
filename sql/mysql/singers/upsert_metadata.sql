INSERT INTO singers (
    name,
    normalized_name,
    gender,
    status,
    created_at,
    updated_at
) VALUES (
    ?,
    ?,
    0,
    1,
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
) ON DUPLICATE KEY UPDATE
    id = LAST_INSERT_ID(id),
    name = COALESCE(NULLIF(name, ''), VALUES(name)),
    gender = 0,
    status = 1,
    updated_at = VALUES(updated_at);
