SELECT
    a.id,
    a.title,
    a.normalized_title,
    a.primary_artist_name,
    a.release_date,
    r.id,
    r.source,
    r.source_item_key,
    r.source_album_id,
    COALESCE(r.source_resource_type, ''),
    COALESCE(r.source_res_type, ''),
    r.source_album_kind,
    COALESCE(r.source_title, ''),
    COALESCE(r.source_artist_id, ''),
    COALESCE(r.source_artist_name, ''),
    COALESCE(r.cover_url, ''),
    COALESCE(r.total_count, ''),
    COALESCE(r.publish_date, ''),
    r.play_num,
    COALESCE(r.play_num_desc, ''),
    r.detail_sync_status,
    r.detail_synced_at,
    COALESCE(r.detail_sync_error, ''),
    COALESCE((SELECT bi.batch_id FROM crawl_batch_items bi WHERE bi.source = r.source AND bi.item_type = 'albumDetail' AND bi.source_album_id = r.source_album_id ORDER BY bi.updated_at DESC, bi.id DESC LIMIT 1), ''),
    COALESCE((SELECT bi.batch_id FROM crawl_batch_items bi WHERE bi.source = r.source AND bi.item_type = 'albumDetail' AND bi.source_album_id = r.source_album_id AND bi.state = 'Running' ORDER BY bi.updated_at DESC, bi.id DESC LIMIT 1), ''),
    r.songs_sync_status,
    r.songs_synced_at,
    COALESCE((SELECT COUNT(DISTINCT abr.song_source_ref_id) FROM album_song_refs abr WHERE abr.source = r.source AND abr.source_album_id = r.source_album_id), 0),
    COALESCE(r.songs_sync_error, ''),
    COALESCE((SELECT bi.batch_id FROM crawl_batch_items bi WHERE bi.source = r.source AND bi.item_type = 'albumSongs' AND bi.source_album_id = r.source_album_id ORDER BY bi.updated_at DESC, bi.id DESC LIMIT 1), ''),
    COALESCE((SELECT bi.batch_id FROM crawl_batch_items bi WHERE bi.source = r.source AND bi.item_type = 'albumSongs' AND bi.source_album_id = r.source_album_id AND bi.state = 'Running' ORDER BY bi.updated_at DESC, bi.id DESC LIMIT 1), ''),
    r.created_at,
    r.updated_at
FROM album_source_refs r
JOIN albums a ON a.id = r.album_db_id
WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND r.source_album_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
LIMIT 1
