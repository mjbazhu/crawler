SELECT COUNT(*)
FROM album_source_refs
WHERE source = 'migu'
  AND CONCAT('|', COALESCE(source_artist_id, ''), '|') LIKE CONCAT('%|', ?, '|%')
