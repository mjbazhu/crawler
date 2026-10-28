UPDATE singer_source_refs
SET songs_sync_status = 0,
    songs_synced_at = 0,
    songs_sync_count = 0,
    songs_sync_error = NULL,
    songs_claim_token = '',
    songs_claimed_at = 0,
    songs_claim_owner = '',
    updated_at = UNIX_TIMESTAMP()
WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (? = '' OR source_singer_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND songs_sync_status IN (?, ?, ?, ?, ?)
  AND (? = '' OR singer_db_id IN (SELECT s.id FROM singers s WHERE s.name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%')) OR source_singer_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%'))
  AND (? = '' OR source_country = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = -1 OR source_gender = ?)
ORDER BY updated_at ASC, id ASC
LIMIT ?
