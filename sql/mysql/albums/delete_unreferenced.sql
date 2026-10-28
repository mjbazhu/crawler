DELETE FROM albums
WHERE id = ? AND NOT EXISTS (SELECT 1 FROM album_source_refs r WHERE r.album_db_id = albums.id);
