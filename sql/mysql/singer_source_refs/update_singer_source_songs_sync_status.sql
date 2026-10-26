UPDATE singer_source_refs
SET
    songs_sync_status = ?,
    songs_synced_at = CASE WHEN ? = 2 THEN UNIX_TIMESTAMP() ELSE songs_synced_at END,
    songs_sync_count = ?,
    songs_sync_error = NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''),
    songs_claim_token = '',
    songs_claimed_at = 0,
    songs_claim_owner = '',
    updated_at = UNIX_TIMESTAMP()
WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND source_singer_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
