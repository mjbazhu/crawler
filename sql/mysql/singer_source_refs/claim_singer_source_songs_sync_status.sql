UPDATE singer_source_refs
SET
    songs_sync_status = 1,
    songs_sync_error = NULL,
    updated_at = UNIX_TIMESTAMP()
WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND source_singer_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (
      songs_sync_status IN (?, ?, ?, ?, ?)
      OR (? = 1 AND songs_sync_status = 1 AND updated_at <= UNIX_TIMESTAMP() - ?)
      OR (
          source_song_total IS NOT NULL
          AND COALESCE(songs_sync_error, '') = ''
          AND source_song_total > (
              SELECT COUNT(DISTINCT ar.song_source_ref_id)
              FROM song_artist_refs ar
              JOIN song_source_refs sr ON sr.id = ar.song_source_ref_id
              WHERE sr.source = singer_source_refs.source
                AND ar.singer_source_ref_id = singer_source_refs.id
          )
      )
  )
  AND NOT (
      COALESCE(songs_sync_error, '') = ''
      AND source_song_total IS NOT NULL
      AND source_song_total <= (
          SELECT COUNT(DISTINCT ar.song_source_ref_id)
          FROM song_artist_refs ar
          JOIN song_source_refs sr ON sr.id = ar.song_source_ref_id
          WHERE sr.source = singer_source_refs.source
            AND ar.singer_source_ref_id = singer_source_refs.id
      )
  )
