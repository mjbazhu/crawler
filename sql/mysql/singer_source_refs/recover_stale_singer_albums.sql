UPDATE singer_source_refs
SET albums_sync_status = 3,
    albums_sync_error = NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''),
    albums_claim_token = '',
    albums_claimed_at = 0,
    albums_claim_owner = '',
    updated_at = UNIX_TIMESTAMP()
WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND albums_sync_status = 1
  AND updated_at <= UNIX_TIMESTAMP() - ?
