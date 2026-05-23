UPDATE song_resources
SET remote_url = COALESCE(NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''), remote_url),
    local_path = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    size_bytes = ?,
    checksum = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    status = 2,
    last_error = NULL,
    error_category = NULL,
    result_code = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    last_batch_id = COALESCE(NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''), last_batch_id),
    running_batch_id = NULL,
    last_processed_at = UNIX_TIMESTAMP(),
    fetched_at = UNIX_TIMESTAMP(),
    updated_at = UNIX_TIMESTAMP()
WHERE id = ?
