SELECT status,claim_generation,COALESCE(local_path,'') FROM album_resources WHERE id=?
