UPDATE singer_source_refs
SET
    albums_sync_status = 1,
    albums_sync_error = NULL,
    updated_at = UNIX_TIMESTAMP()
WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND source_singer_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (
      albums_sync_status IN (?, ?, ?, ?, ?)
      OR (? = 1 AND albums_sync_status = 1 AND updated_at <= UNIX_TIMESTAMP() - ?)
      OR (
          source_album_total IS NOT NULL
          AND COALESCE(albums_sync_error, '') = ''
          AND source_album_total > (
              SELECT COUNT(DISTINCT al.id)
              FROM album_source_refs al
              WHERE al.source = singer_source_refs.source
                AND EXISTS (SELECT 1 FROM album_artist_refs aa WHERE aa.album_source_ref_id=al.id AND aa.singer_source_ref_id=singer_source_refs.id)
          )
      )
  )
  AND NOT (
      COALESCE(albums_sync_error, '') = ''
      AND source_album_total IS NOT NULL
      AND source_album_total <= (
          SELECT COUNT(DISTINCT al.id)
          FROM album_source_refs al
          WHERE al.source = singer_source_refs.source
            AND EXISTS (SELECT 1 FROM album_artist_refs aa WHERE aa.album_source_ref_id=al.id AND aa.singer_source_ref_id=singer_source_refs.id)
      )
  )
