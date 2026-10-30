SELECT status,claim_generation,COALESCE(local_path,'') FROM song_resource_artifacts WHERE id=?
