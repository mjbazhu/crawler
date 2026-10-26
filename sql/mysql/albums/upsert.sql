INSERT INTO albums
    (title, normalized_title, release_date, identity_hash, created_at, updated_at)
VALUES
    (?, ?, '', SHA2(CONCAT_WS('|', 'local-title', ?), 256), ?, ?)
ON DUPLICATE KEY UPDATE
    id = LAST_INSERT_ID(id),
    title = VALUES(title),
    identity_hash = VALUES(identity_hash),
    updated_at = VALUES(updated_at)
