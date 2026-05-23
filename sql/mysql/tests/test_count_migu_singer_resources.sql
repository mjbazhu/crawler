SELECT COUNT(*)
FROM song_resources sr
JOIN song_source_refs r ON r.id = sr.song_source_ref_id
JOIN song_artist_refs ar ON ar.song_source_ref_id = r.id
WHERE r.source = 'migu'
  AND ar.source_artist_id = ?
