SELECT COUNT(*)
FROM (
    SELECT
        CASE
            WHEN base.has_songs = 1 AND base.songs_sync_error = '' AND base.raw_songs_sync_status <> 1 THEN 2
            ELSE base.raw_songs_sync_status
        END AS songs_sync_status,
        CASE
            WHEN base.has_albums = 1 AND base.albums_sync_error = '' AND base.raw_albums_sync_status <> 1 THEN 2
            ELSE base.raw_albums_sync_status
        END AS albums_sync_status,
        base.songs_sync_count,
        base.albums_sync_count
    FROM (
        SELECT
            r.songs_sync_status AS raw_songs_sync_status,
            COALESCE(r.songs_sync_error, '') AS songs_sync_error,
            EXISTS (
                SELECT 1
                FROM song_artist_refs ar
                JOIN song_source_refs sr ON sr.id = ar.song_source_ref_id
                WHERE sr.source = r.source
                  AND ar.singer_source_ref_id = r.id
                LIMIT 1
            ) AS has_songs,
            (
                SELECT COUNT(DISTINCT ar.song_source_ref_id)
                FROM song_artist_refs ar
                JOIN song_source_refs sr ON sr.id = ar.song_source_ref_id
                WHERE sr.source = r.source
                  AND ar.singer_source_ref_id = r.id
            ) AS songs_sync_count,
            r.albums_sync_status AS raw_albums_sync_status,
            COALESCE(r.albums_sync_error, '') AS albums_sync_error,
            EXISTS (
                SELECT 1
                FROM album_source_refs al
                WHERE al.source = r.source
                  AND al.source_artist_id = r.source_singer_id
                LIMIT 1
            ) AS has_albums,
            (
                SELECT COUNT(DISTINCT al.id)
                FROM album_source_refs al
                WHERE al.source = r.source
                  AND al.source_artist_id = r.source_singer_id
            ) AS albums_sync_count
        FROM singer_source_refs r
        JOIN singers s ON s.id = r.singer_db_id
        WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
          AND (? = '' OR s.name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR r.source_singer_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%'))
          AND (? = '' OR r.source_country = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
          AND (? = -1 OR s.gender = ?)
    ) base
) summary
WHERE (? = 0 OR summary.songs_sync_status IN (?, ?, ?, ?, ?))
  AND (? = 0 OR summary.albums_sync_status IN (?, ?, ?, ?, ?))
  AND (? = -1 OR summary.songs_sync_count >= ?)
  AND (? = -1 OR summary.songs_sync_count <= ?)
  AND (? = -1 OR summary.albums_sync_count >= ?)
  AND (? = -1 OR summary.albums_sync_count <= ?)
