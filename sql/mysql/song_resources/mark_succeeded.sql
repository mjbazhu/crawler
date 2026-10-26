UPDATE song_resources
SET local_path = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    cloud_backend = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    cloud_path = NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''),
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
    cloud_uploaded_at = IF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci = '', 0, UNIX_TIMESTAMP()),
    updated_at = UNIX_TIMESTAMP()
WHERE id = ?
