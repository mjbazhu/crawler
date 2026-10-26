UPDATE crawl_batches
SET archived_at = UNIX_TIMESTAMP(),
    archived_reason = NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''),
    updated_at = UNIX_TIMESTAMP()
WHERE batch_id = ?
