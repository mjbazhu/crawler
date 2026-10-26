UPDATE album_source_refs
SET
    detail_sync_status = ?,
    detail_synced_at = CASE WHEN ? = 2 THEN UNIX_TIMESTAMP() ELSE detail_synced_at END,
    detail_sync_error = NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''),
    detail_claim_token = '',
    detail_claimed_at = 0,
    detail_claim_owner = '',
    updated_at = UNIX_TIMESTAMP()
WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND source_album_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
