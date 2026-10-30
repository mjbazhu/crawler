SELECT status,claim_generation,COALESCE(local_path,'') FROM singer_resources WHERE id=?
