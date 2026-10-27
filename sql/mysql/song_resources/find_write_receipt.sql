SELECT status,claim_generation,COALESCE(local_path,'') FROM song_resources WHERE id = ? FOR UPDATE;
