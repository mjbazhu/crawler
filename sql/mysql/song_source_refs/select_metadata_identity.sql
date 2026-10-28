SELECT COALESCE(source_song_id, ''), COALESCE(source_content_id, '')
FROM song_source_refs
WHERE id=?
FOR UPDATE;
