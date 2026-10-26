UPDATE album_source_refs
SET detail_sync_status = 3,
    detail_sync_error = NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''),
    detail_claim_token = '',
    detail_claimed_at = 0,
    detail_claim_owner = '',
    updated_at = UNIX_TIMESTAMP()
WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND detail_sync_status = 1
  AND updated_at <= UNIX_TIMESTAMP() - ?
