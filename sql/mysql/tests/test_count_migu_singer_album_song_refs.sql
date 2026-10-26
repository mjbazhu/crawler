SELECT COUNT(*)
FROM song_catalog_sources
WHERE source = 'migu'
  AND source_album_id IN (
      SELECT source_album_id
      FROM album_catalog_sources
      WHERE source = 'migu'
        AND CONCAT('|', COALESCE(source_artist_id, ''), '|') LIKE CONCAT('%|', ?, '|%')
  )
