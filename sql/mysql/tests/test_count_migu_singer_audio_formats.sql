SELECT COUNT(*)
FROM song_source_audio_formats f
JOIN song_source_refs r ON r.id = f.song_source_ref_id
JOIN song_artist_refs ar ON ar.song_source_ref_id = r.id
WHERE r.source = 'migu'
  AND ar.source_artist_id = ?
