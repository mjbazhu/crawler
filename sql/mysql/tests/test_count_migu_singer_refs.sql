SELECT COUNT(*)
FROM song_source_refs r
JOIN song_artist_refs ar ON ar.song_source_ref_id = r.id
WHERE r.source = 'migu'
  AND ar.source_artist_id = ?
