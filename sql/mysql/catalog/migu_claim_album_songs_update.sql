UPDATE album_source_refs
SET songs_sync_status = 1,
    songs_sync_error = NULL,
    songs_claim_token = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    songs_claimed_at = UNIX_TIMESTAMP(),
    songs_claim_owner = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    updated_at = UNIX_TIMESTAMP()
WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (? = '' OR source_album_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND detail_sync_status = 2
  AND songs_sync_status IN (?, ?, ?, ?, ?)
  AND (? = '' OR source_title LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR source_album_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%'))
  AND (? = '' OR source_artist_name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR source_artist_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = -1 OR source_album_kind = ?)
ORDER BY updated_at ASC, id ASC
LIMIT ?
