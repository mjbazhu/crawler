WITH base_candidates AS (
    SELECT
        s.id AS singer_db_id,
        s.name,
        s.normalized_name,
        COALESCE(r.source_name_pinyin, '') AS source_name_pinyin,
        r.source_gender AS gender,
        s.status,
        r.id AS source_ref_id,
        r.source,
        CONCAT('singer:',r.source_singer_id) AS source_item_key,
        r.source_singer_id,
        COALESCE(r.source_country, '') AS source_country,
        COALESCE((SELECT m.source_tag FROM singer_index_memberships m WHERE m.singer_source_ref_id=r.id AND NULLIF(m.source_tag,'') IS NOT NULL ORDER BY m.id LIMIT 1), '') AS source_tag,
        r.source_song_total,
        r.source_album_total,
        COALESCE((SELECT resource.remote_url FROM singer_resources resource WHERE resource.singer_source_ref_id=r.id AND resource.resource_kind='cover'), '') AS avatar_url,
        r.songs_sync_status AS raw_songs_sync_status,
        r.songs_synced_at AS raw_songs_synced_at,
        COALESCE(r.songs_sync_error, '') AS songs_sync_error,
        r.albums_sync_status AS raw_albums_sync_status,
        r.albums_synced_at AS raw_albums_synced_at,
        COALESCE(r.albums_sync_error, '') AS albums_sync_error,
        r.created_at,
        r.updated_at
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
        base.singer_db_id,
        base.name,
        base.normalized_name,
        base.source_name_pinyin,
        base.gender,
        base.status,
        base.source_ref_id,
        base.source,
        base.source_item_key,
        base.source_singer_id,
        base.source_country,
        base.source_tag,
        base.source_song_total,
        base.source_album_total,
        base.avatar_url,
        CASE
            WHEN base.source_song_total IS NOT NULL AND COALESCE(sc.songs_sync_count, 0) >= base.source_song_total AND base.songs_sync_error = '' AND base.raw_songs_sync_status <> 1 THEN 2
            WHEN base.source_song_total IS NOT NULL AND COALESCE(sc.songs_sync_count, 0) < base.source_song_total AND base.songs_sync_error = '' AND base.raw_songs_sync_status <> 1 THEN 4
            ELSE base.raw_songs_sync_status
        END AS songs_sync_status,
        CASE
            WHEN base.source_song_total IS NOT NULL AND COALESCE(sc.songs_sync_count, 0) >= base.source_song_total AND base.songs_sync_error = '' AND base.raw_songs_synced_at = 0 THEN base.updated_at
            ELSE base.raw_songs_synced_at
        END AS songs_synced_at,
        COALESCE(sc.songs_sync_count, 0) AS songs_sync_count,
        base.songs_sync_error,
        CASE
            WHEN base.source_album_total IS NOT NULL AND COALESCE(ac.albums_sync_count, 0) >= base.source_album_total AND base.albums_sync_error = '' AND base.raw_albums_sync_status <> 1 THEN 2
            WHEN base.source_album_total IS NOT NULL AND COALESCE(ac.albums_sync_count, 0) < base.source_album_total AND base.albums_sync_error = '' AND base.raw_albums_sync_status <> 1 THEN 4
            ELSE base.raw_albums_sync_status
        END AS albums_sync_status,
        CASE
            WHEN base.source_album_total IS NOT NULL AND COALESCE(ac.albums_sync_count, 0) >= base.source_album_total AND base.albums_sync_error = '' AND base.raw_albums_synced_at = 0 THEN base.updated_at
            ELSE base.raw_albums_synced_at
        END AS albums_synced_at,
        COALESCE(ac.albums_sync_count, 0) AS albums_sync_count,
        base.albums_sync_error,
        base.created_at,
        base.updated_at
    FROM base_candidates base
    LEFT JOIN song_counts sc ON sc.source_ref_id = base.source_ref_id
    LEFT JOIN album_counts ac ON ac.source_ref_id = base.source_ref_id
),
page_singers AS (
    SELECT *
    FROM summary
    WHERE (? = 0 OR songs_sync_status IN (?, ?, ?, ?, ?))
      AND (? = 0 OR albums_sync_status IN (?, ?, ?, ?, ?))
      AND (? = -1 OR songs_sync_count >= ?)
      AND (? = -1 OR songs_sync_count <= ?)
      AND (? = -1 OR albums_sync_count >= ?)
      AND (? = -1 OR albums_sync_count <= ?)
    ORDER BY (source_singer_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci) DESC, updated_at DESC, source_ref_id DESC
    LIMIT ? OFFSET ?
),
batch_candidates AS (
    SELECT
        ps.source_ref_id,
        bi.item_type,
        bi.state,
        bi.batch_id,
        ROW_NUMBER() OVER (PARTITION BY ps.source_ref_id, bi.item_type ORDER BY bi.updated_at DESC, bi.id DESC) AS latest_rank,
        ROW_NUMBER() OVER (PARTITION BY ps.source_ref_id, bi.item_type, bi.state ORDER BY bi.updated_at DESC, bi.id DESC) AS state_rank
    FROM page_singers ps
    JOIN crawl_batch_items bi ON bi.source = ps.source
     AND bi.source_singer_id = ps.source_singer_id
     AND bi.item_type IN ('singerSongs', 'singerAlbums')
)
SELECT
    ps.singer_db_id,
    ps.name,
    ps.normalized_name,
    ps.source_name_pinyin,
    ps.gender,
    ps.status,
    ps.source_ref_id,
    ps.source,
    ps.source_item_key,
    ps.source_singer_id,
    ps.source_country,
    ps.source_tag,
    ps.source_song_total,
    ps.source_album_total,
    ps.avatar_url,
    ps.songs_sync_status,
    ps.songs_synced_at,
    ps.songs_sync_count,
    ps.songs_sync_error,
    COALESCE(songs_latest.batch_id, '') AS songs_last_batch_id,
    COALESCE(songs_running.batch_id, '') AS songs_running_batch_id,
    ps.albums_sync_status,
    ps.albums_synced_at,
    ps.albums_sync_count,
    ps.albums_sync_error,
    COALESCE(albums_latest.batch_id, '') AS albums_last_batch_id,
    COALESCE(albums_running.batch_id, '') AS albums_running_batch_id,
    ps.created_at,
    ps.updated_at
FROM page_singers ps
LEFT JOIN batch_candidates songs_latest ON songs_latest.source_ref_id = ps.source_ref_id
 AND songs_latest.item_type = 'singerSongs'
 AND songs_latest.latest_rank = 1
LEFT JOIN batch_candidates songs_running ON songs_running.source_ref_id = ps.source_ref_id
 AND songs_running.item_type = 'singerSongs'
 AND songs_running.state = 'Running'
 AND songs_running.state_rank = 1
LEFT JOIN batch_candidates albums_latest ON albums_latest.source_ref_id = ps.source_ref_id
 AND albums_latest.item_type = 'singerAlbums'
 AND albums_latest.latest_rank = 1
LEFT JOIN batch_candidates albums_running ON albums_running.source_ref_id = ps.source_ref_id
 AND albums_running.item_type = 'singerAlbums'
 AND albums_running.state = 'Running'
 AND albums_running.state_rank = 1
ORDER BY (ps.source_singer_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci) DESC, ps.updated_at DESC, ps.source_ref_id DESC
