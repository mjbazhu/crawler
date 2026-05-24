UPDATE singer_source_refs
SET songs_sync_status = 1,
    songs_sync_error = NULL,
    songs_claim_token = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    songs_claimed_at = UNIX_TIMESTAMP(),
    songs_claim_owner = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    updated_at = UNIX_TIMESTAMP()
WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (? = '' OR source_singer_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND songs_sync_status IN (?, ?, ?, ?, ?)
  AND NOT (
      COALESCE(songs_sync_error, '') = ''
      AND EXISTS (
          SELECT 1
          FROM song_artist_refs ar
          JOIN song_source_refs sr ON sr.id = ar.song_source_ref_id
          WHERE sr.source = singer_source_refs.source
            AND ar.singer_source_ref_id = singer_source_refs.id
      )
  )
  AND (? = '' OR singer_db_id IN (SELECT s.id FROM singers s WHERE s.name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%')) OR source_singer_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%'))
  AND (? = '' OR source_country = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = -1 OR singer_db_id IN (SELECT s.id FROM singers s WHERE s.gender = ?))
ORDER BY updated_at ASC, id ASC
LIMIT ?
