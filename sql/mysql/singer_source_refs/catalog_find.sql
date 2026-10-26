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
        WHEN base.source_song_total IS NOT NULL AND base.songs_sync_count >= base.source_song_total AND base.songs_sync_error = '' AND base.raw_songs_sync_status <> 1 THEN 2
        WHEN base.source_song_total IS NOT NULL AND base.songs_sync_count < base.source_song_total AND base.songs_sync_error = '' AND base.raw_songs_sync_status <> 1 THEN 4
        ELSE base.raw_songs_sync_status
    END AS songs_sync_status,
    CASE
        WHEN base.source_song_total IS NOT NULL AND base.songs_sync_count >= base.source_song_total AND base.songs_sync_error = '' AND base.raw_songs_synced_at = 0 THEN base.updated_at
        ELSE base.raw_songs_synced_at
    END AS songs_synced_at,
    base.songs_sync_count,
    base.songs_sync_error,
    base.songs_last_batch_id,
    base.songs_running_batch_id,
    CASE
        WHEN base.source_album_total IS NOT NULL AND base.albums_sync_count >= base.source_album_total AND base.albums_sync_error = '' AND base.raw_albums_sync_status <> 1 THEN 2
        WHEN base.source_album_total IS NOT NULL AND base.albums_sync_count < base.source_album_total AND base.albums_sync_error = '' AND base.raw_albums_sync_status <> 1 THEN 4
        ELSE base.raw_albums_sync_status
    END AS albums_sync_status,
    CASE
        WHEN base.source_album_total IS NOT NULL AND base.albums_sync_count >= base.source_album_total AND base.albums_sync_error = '' AND base.raw_albums_synced_at = 0 THEN base.updated_at
        ELSE base.raw_albums_synced_at
    END AS albums_synced_at,
    base.albums_sync_count,
    base.albums_sync_error,
    base.albums_last_batch_id,
    base.albums_running_batch_id,
    base.created_at,
    base.updated_at
FROM (
    SELECT
        s.id AS singer_db_id,
        s.name,
        s.normalized_name,
        COALESCE(r.source_name_pinyin, '') AS source_name_pinyin,
        s.gender,
        s.status,
        r.id AS source_ref_id,
        r.source,
        CONCAT('singer:',r.source_singer_id) AS source_item_key,
        r.source_singer_id,
        COALESCE(r.source_country, '') AS source_country,
        COALESCE((SELECT m.source_tag FROM singer_index_memberships m WHERE m.singer_source_ref_id=r.id AND NULLIF(m.source_tag,'') IS NOT NULL ORDER BY m.id LIMIT 1), '') AS source_tag,
        r.source_song_total,
        r.source_album_total,
        '' AS avatar_url,
        r.songs_sync_status AS raw_songs_sync_status,
        r.songs_synced_at AS raw_songs_synced_at,
        (
            SELECT COUNT(DISTINCT ar.song_source_ref_id)
            FROM song_artist_refs ar
            JOIN song_source_refs sr ON sr.id = ar.song_source_ref_id
            WHERE sr.source = r.source
              AND ar.singer_source_ref_id = r.id
        ) AS songs_sync_count,
        COALESCE(r.songs_sync_error, '') AS songs_sync_error,
        COALESCE((SELECT bi.batch_id FROM crawl_batch_items bi WHERE bi.source = r.source AND bi.item_type = 'singerSongs' AND bi.source_singer_id = r.source_singer_id ORDER BY bi.updated_at DESC, bi.id DESC LIMIT 1), '') AS songs_last_batch_id,
        COALESCE((SELECT bi.batch_id FROM crawl_batch_items bi WHERE bi.source = r.source AND bi.item_type = 'singerSongs' AND bi.source_singer_id = r.source_singer_id AND bi.state = 'Running' ORDER BY bi.updated_at DESC, bi.id DESC LIMIT 1), '') AS songs_running_batch_id,
        r.albums_sync_status AS raw_albums_sync_status,
        r.albums_synced_at AS raw_albums_synced_at,
        (
            SELECT COUNT(DISTINCT al.id)
            FROM album_source_refs al
            WHERE al.source = r.source
              AND EXISTS (SELECT 1 FROM album_artist_refs aa WHERE aa.album_source_ref_id=al.id AND aa.singer_source_ref_id=r.id)
        ) AS albums_sync_count,
        COALESCE(r.albums_sync_error, '') AS albums_sync_error,
        COALESCE((SELECT bi.batch_id FROM crawl_batch_items bi WHERE bi.source = r.source AND bi.item_type = 'singerAlbums' AND bi.source_singer_id = r.source_singer_id ORDER BY bi.updated_at DESC, bi.id DESC LIMIT 1), '') AS albums_last_batch_id,
        COALESCE((SELECT bi.batch_id FROM crawl_batch_items bi WHERE bi.source = r.source AND bi.item_type = 'singerAlbums' AND bi.source_singer_id = r.source_singer_id AND bi.state = 'Running' ORDER BY bi.updated_at DESC, bi.id DESC LIMIT 1), '') AS albums_running_batch_id,
        r.created_at,
        r.updated_at
    FROM singer_source_refs r
    JOIN singers s ON s.id = r.singer_db_id
    WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
      AND r.source_singer_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
    LIMIT 1
) base
