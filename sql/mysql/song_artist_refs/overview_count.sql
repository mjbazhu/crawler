SELECT COUNT(*) FROM song_artist_refs ar JOIN song_source_refs sr ON sr.id = ar.song_source_ref_id WHERE sr.source = 'migu'
