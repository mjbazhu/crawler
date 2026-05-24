INSERT INTO song_resources (
    song_source_ref_id,
    resource_kind,
    format_type,
    remote_url,
    local_path,
    size_bytes,
    checksum,
    status,
    retry_count,
    last_error,
    error_category,
    result_code,
    running_batch_id,
    last_processed_at,
    fetched_at,
    created_at,
    updated_at
)
SELECT
    ?,
    CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    '',
    NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''),
    NULL,
    0,
    NULL,
    1,
    0,
    NULL,
    NULL,
    NULL,
    NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''),
    UNIX_TIMESTAMP(),
    0,
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
WHERE NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '') IS NOT NULL
  AND EXISTS (SELECT 1 FROM song_source_refs WHERE id = ?)
ON DUPLICATE KEY UPDATE
    remote_url = IF(status IN (0, 3), VALUES(remote_url), remote_url),
    last_error = IF(status IN (0, 3), NULL, last_error),
    error_category = IF(status IN (0, 3), NULL, error_category),
    result_code = IF(status IN (0, 3), NULL, result_code),
    running_batch_id = IF(status IN (0, 3), VALUES(running_batch_id), running_batch_id),
    last_processed_at = IF(status IN (0, 3), VALUES(last_processed_at), last_processed_at),
    updated_at = IF(status IN (0, 3), VALUES(updated_at), updated_at),
    status = IF(status IN (0, 3), VALUES(status), status)
