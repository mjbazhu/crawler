SELECT
    (SELECT COUNT(*)
     FROM singer_catalog_sources r
     WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
       AND NOT EXISTS (
           SELECT 1
           FROM song_artist_refs ar
           JOIN song_catalog_sources sr ON sr.id = ar.song_source_ref_id
           WHERE sr.source = r.source
             AND ar.singer_source_ref_id = r.id
       )) AS singers_without_songs,
    (SELECT COUNT(*)
     FROM singer_catalog_sources r
     WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
       AND NOT EXISTS (
           SELECT 1
           FROM album_catalog_sources al
           WHERE al.source = r.source
             AND EXISTS (SELECT 1 FROM album_artist_refs aa WHERE aa.album_source_ref_id=al.id AND aa.singer_source_ref_id=r.id)
       )) AS singers_without_albums,
    (SELECT COUNT(*) FROM album_catalog_sources WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci AND detail_sync_status <> 2) AS albums_without_details,
    (SELECT COUNT(*) FROM album_catalog_sources WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci AND detail_sync_status = 2 AND songs_sync_status <> 2) AS albums_without_songs,
    (SELECT COUNT(*)
     FROM song_catalog_sources r
     WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
       AND NOT EXISTS (SELECT 1 FROM song_resources sr WHERE sr.song_source_ref_id = r.id)) AS songs_without_resource_tasks,
    (SELECT COUNT(*)
     FROM song_resources sr
     JOIN song_catalog_sources r ON r.id = sr.song_source_ref_id
     WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
       AND sr.status = 3) AS resources_failed,
    (SELECT COUNT(*)
     FROM song_resources sr
     JOIN song_catalog_sources r ON r.id = sr.song_source_ref_id
     WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
       AND sr.status = 1
       AND sr.updated_at <= UNIX_TIMESTAMP() - ?) AS resources_running_stale,
    (SELECT COUNT(*) FROM singer_catalog_sources WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci AND songs_sync_status = 1 AND updated_at <= UNIX_TIMESTAMP() - ?) AS singers_songs_running_stale,
    (SELECT COUNT(*) FROM singer_catalog_sources WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci AND albums_sync_status = 1 AND updated_at <= UNIX_TIMESTAMP() - ?) AS singers_albums_running_stale,
    (SELECT COUNT(*) FROM album_catalog_sources WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci AND detail_sync_status = 1 AND updated_at <= UNIX_TIMESTAMP() - ?) AS albums_details_running_stale,
    (SELECT COUNT(*) FROM album_catalog_sources WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci AND songs_sync_status = 1 AND updated_at <= UNIX_TIMESTAMP() - ?) AS albums_songs_running_stale,
    (SELECT COUNT(*) FROM crawl_batches WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci AND state = 'Running' AND updated_at <= UNIX_TIMESTAMP() - ?) AS running_batches_stale,
    UNIX_TIMESTAMP() AS generated_at
