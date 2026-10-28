INSERT INTO singer_resources (singer_source_ref_id, resource_kind, remote_url, status, created_at, updated_at)
VALUES (?, 'cover', NULLIF(?, ''), 0, UNIX_TIMESTAMP(), UNIX_TIMESTAMP())
ON DUPLICATE KEY UPDATE
    id = LAST_INSERT_ID(id),
    remote_url = COALESCE(VALUES(remote_url), remote_url),
    updated_at = VALUES(updated_at);
