UPDATE album_source_refs
SET songs_sync_status = 3,
    songs_sync_error = NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''),
    songs_claim_token = '',
    songs_claimed_at = 0,
    songs_claim_owner = '',
    updated_at = UNIX_TIMESTAMP()
WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND songs_sync_status = 1
  AND updated_at <= UNIX_TIMESTAMP() - ?
