SELECT COUNT(*) FROM album_song_refs ar JOIN album_source_refs a ON a.id=ar.album_source_ref_id WHERE a.source='migu'
