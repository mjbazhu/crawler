SELECT s.id,s.source,s.source_album_id,r.remote_url FROM album_resources r JOIN album_source_refs s ON s.id=r.album_source_ref_id WHERE r.id=?
