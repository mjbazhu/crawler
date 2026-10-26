SELECT COUNT(*) FROM album_artist_refs aa JOIN album_source_refs a ON a.id=aa.album_source_ref_id WHERE a.source='migu'
