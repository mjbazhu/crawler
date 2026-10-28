SELECT status,claim_generation,COALESCE(result_code,'') FROM song_resources WHERE id = ?;
