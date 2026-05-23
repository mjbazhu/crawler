UPDATE album_source_refs
SET songs_sync_status = 0,
    songs_synced_at = 0,
    songs_sync_count = 0,
    songs_sync_error = NULL,
    songs_claim_token = '',
    songs_claimed_at = 0,
    songs_claim_owner = '',
    updated_at = UNIX_TIMESTAMP()
WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (? = '' OR source_album_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND songs_sync_status IN (?, ?, ?, ?, ?)
  AND (? = '' OR source_title LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR source_album_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%'))
  AND (? = '' OR source_artist_name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR source_artist_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = -1 OR source_album_kind = ?)
ORDER BY updated_at ASC, id ASC
LIMIT ?
