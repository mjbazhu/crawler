UPDATE song_resources
SET status = 3,
    retry_count = retry_count + 1,
    last_error = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    error_category = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    result_code = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    last_batch_id = COALESCE(NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''), last_batch_id),
    running_batch_id = NULL,
    last_processed_at = UNIX_TIMESTAMP(),
    updated_at = UNIX_TIMESTAMP()
WHERE id = ?
  AND status = 1
  AND claim_generation = ?
