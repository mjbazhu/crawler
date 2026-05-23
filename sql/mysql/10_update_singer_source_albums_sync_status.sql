UPDATE singer_source_refs
SET
    albums_sync_status = ?,
    albums_synced_at = CASE WHEN ? = 2 THEN UNIX_TIMESTAMP() ELSE albums_synced_at END,
    albums_sync_count = ?,
    albums_sync_error = NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''),
    albums_claim_token = '',
    albums_claimed_at = 0,
    albums_claim_owner = '',
    updated_at = UNIX_TIMESTAMP()
WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND source_singer_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
