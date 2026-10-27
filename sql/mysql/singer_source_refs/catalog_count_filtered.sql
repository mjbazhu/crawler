WITH base_candidates AS (
    SELECT
        r.id AS source_ref_id,
        r.source,
        r.source_singer_id,
        r.source_song_total,
        r.source_album_total,
        r.songs_sync_status AS raw_songs_sync_status,
        COALESCE(r.songs_sync_error, '') AS songs_sync_error,
        r.albums_sync_status AS raw_albums_sync_status,
        COALESCE(r.albums_sync_error, '') AS albums_sync_error
    FROM singer_source_refs r
    JOIN singers s ON s.id = r.singer_db_id
    WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
      AND (? = '' OR s.name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR r.source_singer_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%'))
      AND (? = '' OR r.source_singer_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
      AND (? = '' OR r.source_country = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
      AND (? = -1 OR r.source_gender = ?)
),
song_counts AS (
    SELECT
        ar.singer_source_ref_id AS source_ref_id,
        COUNT(DISTINCT ar.song_source_ref_id) AS songs_sync_count
    FROM song_artist_refs ar
    JOIN base_candidates base ON base.source_ref_id = ar.singer_source_ref_id
    JOIN song_source_refs sr ON sr.id = ar.song_source_ref_id
     AND sr.source = base.source
    GROUP BY ar.singer_source_ref_id
),
album_counts AS (
    SELECT
        base.source_ref_id,
        COUNT(DISTINCT al.id) AS albums_sync_count
    FROM base_candidates base
    JOIN album_artist_refs aa ON aa.singer_source_ref_id=base.source_ref_id
    JOIN album_source_refs al ON al.id=aa.album_source_ref_id AND al.source=base.source
    GROUP BY base.source_ref_id
),
summary AS (
    SELECT
        CASE
            WHEN base.source_song_total IS NOT NULL AND COALESCE(sc.songs_sync_count, 0) >= base.source_song_total AND base.songs_sync_error = '' AND base.raw_songs_sync_status <> 1 THEN 2
            WHEN base.source_song_total IS NOT NULL AND COALESCE(sc.songs_sync_count, 0) < base.source_song_total AND base.songs_sync_error = '' AND base.raw_songs_sync_status <> 1 THEN 4
            ELSE base.raw_songs_sync_status
        END AS songs_sync_status,
        COALESCE(sc.songs_sync_count, 0) AS songs_sync_count,
        CASE
            WHEN base.source_album_total IS NOT NULL AND COALESCE(ac.albums_sync_count, 0) >= base.source_album_total AND base.albums_sync_error = '' AND base.raw_albums_sync_status <> 1 THEN 2
            WHEN base.source_album_total IS NOT NULL AND COALESCE(ac.albums_sync_count, 0) < base.source_album_total AND base.albums_sync_error = '' AND base.raw_albums_sync_status <> 1 THEN 4
            ELSE base.raw_albums_sync_status
        END AS albums_sync_status,
        COALESCE(ac.albums_sync_count, 0) AS albums_sync_count
    FROM base_candidates base
    LEFT JOIN song_counts sc ON sc.source_ref_id = base.source_ref_id
    LEFT JOIN album_counts ac ON ac.source_ref_id = base.source_ref_id
)
SELECT COUNT(*)
FROM summary
WHERE (? = 0 OR songs_sync_status IN (?, ?, ?, ?, ?))
  AND (? = 0 OR albums_sync_status IN (?, ?, ?, ?, ?))
  AND (? = -1 OR songs_sync_count >= ?)
  AND (? = -1 OR songs_sync_count <= ?)
  AND (? = -1 OR albums_sync_count >= ?)
  AND (? = -1 OR albums_sync_count <= ?)
