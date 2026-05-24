SELECT
    s.id,
    s.name,
    s.normalized_name,
    COALESCE(r.source_name_pinyin, ''),
    s.gender,
    s.status,
    r.id,
    r.source,
    r.source_item_key,
    r.source_singer_id,
    COALESCE(r.source_country, ''),
    COALESCE(r.source_tag, ''),
    COALESCE(r.source_metric, ''),
    COALESCE(r.avatar_url, ''),
    CASE
        WHEN EXISTS (
            SELECT 1
            FROM song_artist_refs ar
            JOIN song_source_refs sr ON sr.id = ar.song_source_ref_id
            WHERE sr.source = r.source
              AND ar.singer_source_ref_id = r.id
            LIMIT 1
        ) AND COALESCE(r.songs_sync_error, '') = '' AND r.songs_sync_status <> 1 THEN 2
        ELSE r.songs_sync_status
    END,
    CASE
        WHEN EXISTS (
            SELECT 1
            FROM song_artist_refs ar
            JOIN song_source_refs sr ON sr.id = ar.song_source_ref_id
            WHERE sr.source = r.source
              AND ar.singer_source_ref_id = r.id
            LIMIT 1
        ) AND COALESCE(r.songs_sync_error, '') = '' AND r.songs_synced_at = 0 THEN r.updated_at
        ELSE r.songs_synced_at
    END,
    (
        SELECT COUNT(DISTINCT ar.song_source_ref_id)
        FROM song_artist_refs ar
        JOIN song_source_refs sr ON sr.id = ar.song_source_ref_id
        WHERE sr.source = r.source
          AND ar.singer_source_ref_id = r.id
    ),
    COALESCE(r.songs_sync_error, ''),
    COALESCE((SELECT bi.batch_id FROM crawl_batch_items bi WHERE bi.source = r.source AND bi.item_type = 'singerSongs' AND bi.source_singer_id = r.source_singer_id ORDER BY bi.updated_at DESC, bi.id DESC LIMIT 1), ''),
    COALESCE((SELECT bi.batch_id FROM crawl_batch_items bi WHERE bi.source = r.source AND bi.item_type = 'singerSongs' AND bi.source_singer_id = r.source_singer_id AND bi.state = 'Running' ORDER BY bi.updated_at DESC, bi.id DESC LIMIT 1), ''),
    CASE
        WHEN EXISTS (
            SELECT 1
            FROM album_source_refs al
            WHERE al.source = r.source
              AND al.source_artist_id = r.source_singer_id
            LIMIT 1
        ) AND COALESCE(r.albums_sync_error, '') = '' AND r.albums_sync_status <> 1 THEN 2
        ELSE r.albums_sync_status
    END,
    CASE
        WHEN EXISTS (
            SELECT 1
            FROM album_source_refs al
            WHERE al.source = r.source
              AND al.source_artist_id = r.source_singer_id
            LIMIT 1
        ) AND COALESCE(r.albums_sync_error, '') = '' AND r.albums_synced_at = 0 THEN r.updated_at
        ELSE r.albums_synced_at
    END,
    (
        SELECT COUNT(DISTINCT al.id)
        FROM album_source_refs al
        WHERE al.source = r.source
          AND al.source_artist_id = r.source_singer_id
    ),
    COALESCE(r.albums_sync_error, ''),
    COALESCE((SELECT bi.batch_id FROM crawl_batch_items bi WHERE bi.source = r.source AND bi.item_type = 'singerAlbums' AND bi.source_singer_id = r.source_singer_id ORDER BY bi.updated_at DESC, bi.id DESC LIMIT 1), ''),
    COALESCE((SELECT bi.batch_id FROM crawl_batch_items bi WHERE bi.source = r.source AND bi.item_type = 'singerAlbums' AND bi.source_singer_id = r.source_singer_id AND bi.state = 'Running' ORDER BY bi.updated_at DESC, bi.id DESC LIMIT 1), ''),
    r.created_at,
    r.updated_at
FROM singer_source_refs r
JOIN singers s ON s.id = r.singer_db_id
WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND r.albums_claim_token = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
ORDER BY r.albums_claimed_at DESC, r.id ASC
