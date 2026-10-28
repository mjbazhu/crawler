INSERT INTO album_resources (album_source_ref_id, resource_kind, remote_url, local_path, size_bytes, checksum, status, error_category, result_code, last_error, fetched_at, created_at, updated_at)
SELECT ?, previous.resource_kind, previous.remote_url, previous.local_path, previous.size_bytes, previous.checksum, previous.status, previous.error_category, previous.result_code, previous.last_error, previous.fetched_at, previous.created_at, UNIX_TIMESTAMP()
FROM album_resources AS previous WHERE previous.album_source_ref_id = ?
ON DUPLICATE KEY UPDATE
    remote_url = COALESCE(NULLIF(album_resources.remote_url, ''), VALUES(remote_url)),
    local_path = IF(album_resources.status = 2, album_resources.local_path, VALUES(local_path)),
    size_bytes = IF(album_resources.status = 2, album_resources.size_bytes, VALUES(size_bytes)),
    checksum = IF(album_resources.status = 2, album_resources.checksum, VALUES(checksum)),
    error_category = IF(album_resources.status = 2, album_resources.error_category, VALUES(error_category)),
    result_code = IF(album_resources.status = 2, album_resources.result_code, VALUES(result_code)),
    last_error = IF(album_resources.status = 2, album_resources.last_error, VALUES(last_error)),
    fetched_at = IF(album_resources.status = 2, album_resources.fetched_at, VALUES(fetched_at)),
    status = IF(album_resources.status = 2, album_resources.status, VALUES(status)),
    updated_at = VALUES(updated_at);
