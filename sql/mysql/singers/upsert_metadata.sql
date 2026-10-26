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
    ?,
    1,
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
) ON DUPLICATE KEY UPDATE
    id = LAST_INSERT_ID(id),
    name = COALESCE(NULLIF(VALUES(name), ''), name),
    gender = IF(VALUES(gender) = 0, gender, VALUES(gender)),
    status = 1,
    updated_at = VALUES(updated_at);
