INSERT INTO albums (
    title,
    normalized_title,
    release_date,
    identity_hash,
    created_at,
    updated_at
) VALUES (
    ?,
    ?,
    ?,
    SHA2(CONCAT_WS('|', ?, ?), 256),
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
) ON DUPLICATE KEY UPDATE
    id = LAST_INSERT_ID(id),
    title = VALUES(title),
    normalized_title = VALUES(normalized_title),
    release_date = COALESCE(NULLIF(VALUES(release_date), ''), release_date),
    updated_at = VALUES(updated_at);
