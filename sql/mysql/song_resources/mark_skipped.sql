UPDATE song_resources
SET status = 4,
    last_error = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    error_category = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    result_code = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    last_batch_id = COALESCE(NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''), last_batch_id),
    running_batch_id = NULL,
    last_processed_at = UNIX_TIMESTAMP(),
    updated_at = UNIX_TIMESTAMP()
WHERE ((? > 0 AND song_source_ref_id = ?) OR (? = 0 AND id = ?))
  AND (? = '' OR resource_kind = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = '' OR format_type = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND status IN (0, 1, 3)
