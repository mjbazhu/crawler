SELECT COUNT(*)
FROM album_catalog_sources
WHERE source = 'migu'
  AND CONCAT('|', COALESCE(source_artist_id, ''), '|') LIKE CONCAT('%|', ?, '|%')
  AND songs_sync_status = 2
