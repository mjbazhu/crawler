UPDATE song_resources
SET status = 1,
    claim_generation = claim_generation + 1,
    last_error = NULL,
    error_category = NULL,
    result_code = NULL,
    running_batch_id = NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''),
    last_processed_at = UNIX_TIMESTAMP(),
    updated_at = UNIX_TIMESTAMP()
WHERE id = ?
  AND status IN (?, ?, ?, ?, ?)
  AND claim_generation = ?
