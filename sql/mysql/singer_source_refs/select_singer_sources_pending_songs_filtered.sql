SELECT
    summary.id,
    summary.singer_db_id,
    summary.source,
    summary.source_item_key,
    summary.source_singer_id,
    summary.name,
    summary.source_country,
    summary.gender,
    summary.avatar_url,
    summary.source_song_total,
    summary.source_album_total,
    summary.sync_status,
    summary.synced_at,
    summary.sync_count,
    summary.sync_error,
    summary.updated_at
FROM (
    SELECT
        r.id,
        r.singer_db_id,
        r.source,
        CONCAT('singer:',r.source_singer_id) AS source_item_key,
        r.source_singer_id,
        s.name,
        COALESCE(r.source_country, '') AS source_country,
        r.source_gender AS gender,
        '' AS avatar_url,
        r.source_song_total,
        r.source_album_total,
        CASE
            WHEN r.source_song_total IS NOT NULL AND (
                SELECT COUNT(DISTINCT ar.song_source_ref_id)
                FROM song_artist_refs ar
                JOIN song_source_refs sr ON sr.id = ar.song_source_ref_id
                WHERE sr.source = r.source
                  AND ar.singer_source_ref_id = r.id
            ) >= r.source_song_total AND COALESCE(r.songs_sync_error, '') = '' AND r.songs_sync_status <> 1 THEN 2
            WHEN r.source_song_total IS NOT NULL AND (
                SELECT COUNT(DISTINCT ar.song_source_ref_id)
                FROM song_artist_refs ar
                JOIN song_source_refs sr ON sr.id = ar.song_source_ref_id
                WHERE sr.source = r.source
                  AND ar.singer_source_ref_id = r.id
            ) < r.source_song_total AND COALESCE(r.songs_sync_error, '') = '' AND r.songs_sync_status <> 1 THEN 4
            ELSE r.songs_sync_status
        END AS sync_status,
        CASE
            WHEN r.source_song_total IS NOT NULL AND (
                SELECT COUNT(DISTINCT ar.song_source_ref_id)
                FROM song_artist_refs ar
                JOIN song_source_refs sr ON sr.id = ar.song_source_ref_id
                WHERE sr.source = r.source
                  AND ar.singer_source_ref_id = r.id
            ) >= r.source_song_total AND COALESCE(r.songs_sync_error, '') = '' AND r.songs_synced_at = 0 THEN r.updated_at
            ELSE r.songs_synced_at
        END AS synced_at,
        (
            SELECT COUNT(DISTINCT ar.song_source_ref_id)
            FROM song_artist_refs ar
            JOIN song_source_refs sr ON sr.id = ar.song_source_ref_id
            WHERE sr.source = r.source
              AND ar.singer_source_ref_id = r.id
        ) AS sync_count,
        COALESCE(r.songs_sync_error, '') AS sync_error,
        r.updated_at
    FROM singer_source_refs r
    JOIN singers s ON s.id = r.singer_db_id
    WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
) summary
WHERE (
      (summary.sync_status <> 1 AND summary.sync_status IN (?, ?, ?, ?, ?))
      OR (? = 1 AND summary.sync_status = 1 AND summary.updated_at <= UNIX_TIMESTAMP() - ?)
  )
  AND (? = '' OR summary.source_country = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = -1 OR summary.gender = ?)
  AND (
      ? = ''
      OR summary.name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%')
      OR summary.source_singer_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%')
  )
ORDER BY summary.updated_at ASC, summary.id ASC
LIMIT ? OFFSET ?
