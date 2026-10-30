SELECT s.id,s.source,s.source_singer_id,r.remote_url FROM singer_resources r JOIN singer_source_refs s ON s.id=r.singer_source_ref_id WHERE r.id=?
