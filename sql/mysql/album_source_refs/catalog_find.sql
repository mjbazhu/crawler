WITH lookup AS (
    SELECT CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci AS source,
           CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci AS source_album_id
), candidates AS (
    SELECT r.id,r.updated_at,0 AS priority
    FROM lookup input
    JOIN album_source_refs r ON r.source=input.source AND r.source_album_id=input.source_album_id
    UNION ALL
    SELECT r.id,r.updated_at,1 AS priority
    FROM lookup input
    JOIN migu_dalbum_details d ON d.source_material_id=input.source_album_id
    JOIN album_source_refs r ON r.id=d.album_source_ref_id AND r.source=input.source
), page AS (
    SELECT id,updated_at FROM candidates ORDER BY priority
    LIMIT 1
)
SELECT
    a.id,
    a.title,
    a.normalized_title,
    COALESCE((SELECT sn.name FROM album_artist_refs aa JOIN singer_source_refs sr ON sr.id=aa.singer_source_ref_id JOIN singers sn ON sn.id=sr.singer_db_id WHERE aa.album_source_ref_id=r.id ORDER BY aa.is_primary DESC,aa.is_seed DESC,aa.artist_order,aa.id LIMIT 1),''),
    a.release_date,
    r.id,
    r.source,
    CONCAT('album:',r.source_album_id) AS source_item_key,
    r.source_album_id,
    COALESCE(r.source_resource_type, ''),
    CASE WHEN r.source_resource_type='5' THEN '1' ELSE '0' END,
    r.source_album_kind,
    a.title,
    COALESCE((SELECT sr.source_singer_id FROM album_artist_refs aa JOIN singer_source_refs sr ON sr.id=aa.singer_source_ref_id WHERE aa.album_source_ref_id=r.id ORDER BY aa.is_primary DESC,aa.is_seed DESC,aa.artist_order,aa.id LIMIT 1),''),
    COALESCE((SELECT sn.name FROM album_artist_refs aa JOIN singer_source_refs sr ON sr.id=aa.singer_source_ref_id JOIN singers sn ON sn.id=sr.singer_db_id WHERE aa.album_source_ref_id=r.id ORDER BY aa.is_primary DESC,aa.is_seed DESC,aa.artist_order,aa.id LIMIT 1),''),
    COALESCE((SELECT resource.remote_url FROM album_resources resource WHERE resource.album_source_ref_id=r.id AND resource.resource_kind='cover'), ''),
    COALESCE(CAST(r.source_song_total AS CHAR),''),
    COALESCE(a.release_date,''),
    0,
    '',
    r.detail_sync_status,
    r.detail_synced_at,
    COALESCE(r.detail_sync_error, ''),
    COALESCE((SELECT bi.batch_id FROM crawl_batch_items bi WHERE bi.source = r.source AND bi.item_type = 'albumDetail' AND bi.source_album_id = r.source_album_id ORDER BY bi.updated_at DESC, bi.id DESC LIMIT 1), ''),
    COALESCE((SELECT bi.batch_id FROM crawl_batch_items bi WHERE bi.source = r.source AND bi.item_type = 'albumDetail' AND bi.source_album_id = r.source_album_id AND bi.state = 'Running' ORDER BY bi.updated_at DESC, bi.id DESC LIMIT 1), ''),
    r.songs_sync_status,
    r.songs_synced_at,
    r.songs_sync_count,
    COALESCE(r.songs_sync_error, ''),
    COALESCE((SELECT bi.batch_id FROM crawl_batch_items bi WHERE bi.source = r.source AND bi.item_type = 'albumSongs' AND bi.source_album_id = r.source_album_id ORDER BY bi.updated_at DESC, bi.id DESC LIMIT 1), ''),
    COALESCE((SELECT bi.batch_id FROM crawl_batch_items bi WHERE bi.source = r.source AND bi.item_type = 'albumSongs' AND bi.source_album_id = r.source_album_id AND bi.state = 'Running' ORDER BY bi.updated_at DESC, bi.id DESC LIMIT 1), ''),
    r.created_at,
    r.updated_at,
    (SELECT COUNT(*) FROM album_song_refs relation_rows WHERE relation_rows.album_source_ref_id=r.id) AS album_songs_count,
    COALESCE(r.source_title_pinyin,''),
    COALESCE(r.summary,'')
FROM page p
JOIN album_source_refs r ON r.id=p.id
JOIN albums a ON a.id=r.album_db_id
ORDER BY p.updated_at DESC,p.id DESC
