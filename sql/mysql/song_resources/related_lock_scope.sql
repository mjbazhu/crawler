SELECT r.id FROM song_resources r WHERE r.song_source_ref_id=(SELECT p.song_source_ref_id FROM song_resources p WHERE p.id=?) ORDER BY r.id FOR UPDATE
