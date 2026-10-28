SELECT
    r.id,
    r.singer_db_id,
    r.source,
    CONCAT('singer:',r.source_singer_id) AS source_item_key,
    r.source_singer_id,
    s.name,
    r.source_name_pinyin,
    COALESCE((SELECT m.source_tag FROM singer_index_memberships m WHERE m.singer_source_ref_id=r.id AND NULLIF(m.source_tag,'') IS NOT NULL ORDER BY m.id LIMIT 1),'') AS source_tag,
    r.source_song_total,
    r.source_album_total,
    r.source_country,
    r.source_gender AS gender,
    '' AS avatar_url,
    r.songs_sync_status,
    r.songs_synced_at,
    r.songs_sync_count,
    r.songs_sync_error,
    r.albums_sync_status,
    r.albums_synced_at,
    r.albums_sync_count,
    r.albums_sync_error,
    r.created_at,
    r.updated_at
FROM singer_source_refs r
JOIN singers s ON s.id = r.singer_db_id
WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND r.source_singer_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
LIMIT 1
