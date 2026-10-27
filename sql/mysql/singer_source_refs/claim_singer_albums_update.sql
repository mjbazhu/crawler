UPDATE singer_source_refs
SET albums_sync_status = 1,
    albums_sync_error = NULL,
    albums_claim_token = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    albums_claimed_at = UNIX_TIMESTAMP(),
    albums_claim_owner = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    updated_at = UNIX_TIMESTAMP()
WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (? = '' OR source_singer_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (
      (albums_sync_status <> 1 AND albums_sync_status IN (?, ?, ?, ?, ?))
      OR (
          albums_sync_status <> 1
          AND source_album_total IS NOT NULL
          AND COALESCE(albums_sync_error, '') = ''
          AND source_album_total > (
              SELECT COUNT(DISTINCT al.id)
              FROM album_source_refs al
              WHERE al.source = singer_source_refs.source
                AND EXISTS (SELECT 1 FROM album_artist_refs aa WHERE aa.album_source_ref_id=al.id AND aa.singer_source_ref_id=singer_source_refs.id)
          )
      )
  )
  AND (? = 1 OR NOT (
      COALESCE(albums_sync_error, '') = ''
      AND source_album_total IS NOT NULL
      AND source_album_total <= (
          SELECT COUNT(DISTINCT al.id)
          FROM album_source_refs al
          WHERE al.source = singer_source_refs.source
            AND EXISTS (SELECT 1 FROM album_artist_refs aa WHERE aa.album_source_ref_id=al.id AND aa.singer_source_ref_id=singer_source_refs.id)
      )
  )
)
  AND (? = '' OR singer_db_id IN (SELECT s.id FROM singers s WHERE s.name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%')) OR source_singer_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%'))
  AND (? = '' OR source_country = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = -1 OR source_gender = ?)
ORDER BY updated_at ASC, id ASC
LIMIT ?
