SELECT
    r.source,
    r.source_item_key,
    r.source_album_id,
    COALESCE(r.source_resource_type, ''),
    COALESCE(r.source_res_type, ''),
    COALESCE(r.source_action, ''),
    r.source_album_kind,
    COALESCE(r.source_title, ''),
    COALESCE(r.source_artist_id, ''),
    COALESCE(r.source_artist_name, ''),
    COALESCE(r.cover_url, ''),
    COALESCE(r.total_count, ''),
    r.detail_sync_status,
    a.title,
    a.normalized_title,
    a.primary_artist_name
FROM album_source_refs r
JOIN albums a ON a.id = r.album_db_id
WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND r.source_album_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
LIMIT 1
