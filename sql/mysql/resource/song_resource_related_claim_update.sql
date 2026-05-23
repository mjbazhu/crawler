UPDATE song_resources
SET status = 1,
    remote_url = COALESCE(NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''), remote_url),
    last_error = NULL,
    error_category = NULL,
    result_code = NULL,
    running_batch_id = NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''),
    last_processed_at = UNIX_TIMESTAMP(),
    updated_at = UNIX_TIMESTAMP()
WHERE song_source_ref_id = ?
  AND resource_kind = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND format_type = ''
  AND status IN (0, 3)
