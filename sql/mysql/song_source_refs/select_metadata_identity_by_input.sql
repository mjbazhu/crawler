SELECT id,COALESCE(source_song_id,''),COALESCE(source_content_id,'')
FROM song_source_refs
WHERE source=? AND ((?<>'' AND source_song_id=?) OR (?<>'' AND source_content_id=?))
ORDER BY id FOR UPDATE
