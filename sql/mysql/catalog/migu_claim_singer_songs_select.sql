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
    r.songs_sync_status,
    r.songs_synced_at,
    r.songs_sync_count,
    COALESCE(r.songs_sync_error, ''),
    COALESCE((SELECT bi.batch_id FROM crawl_batch_items bi WHERE bi.source = r.source AND bi.item_type = 'singerSongs' AND bi.source_singer_id = r.source_singer_id ORDER BY bi.updated_at DESC, bi.id DESC LIMIT 1), ''),
    COALESCE((SELECT bi.batch_id FROM crawl_batch_items bi WHERE bi.source = r.source AND bi.item_type = 'singerSongs' AND bi.source_singer_id = r.source_singer_id AND bi.state = 'Running' ORDER BY bi.updated_at DESC, bi.id DESC LIMIT 1), ''),
    r.albums_sync_status,
    r.albums_synced_at,
    r.albums_sync_count,
    COALESCE(r.albums_sync_error, ''),
    COALESCE((SELECT bi.batch_id FROM crawl_batch_items bi WHERE bi.source = r.source AND bi.item_type = 'singerAlbums' AND bi.source_singer_id = r.source_singer_id ORDER BY bi.updated_at DESC, bi.id DESC LIMIT 1), ''),
    COALESCE((SELECT bi.batch_id FROM crawl_batch_items bi WHERE bi.source = r.source AND bi.item_type = 'singerAlbums' AND bi.source_singer_id = r.source_singer_id AND bi.state = 'Running' ORDER BY bi.updated_at DESC, bi.id DESC LIMIT 1), ''),
    r.created_at,
    r.updated_at
FROM singer_source_refs r
JOIN singers s ON s.id = r.singer_db_id
WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND r.songs_claim_token = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
ORDER BY r.songs_claimed_at DESC, r.id ASC
