SELECT COUNT(*)
FROM song_source_audio_formats f
JOIN song_catalog_sources r ON r.id = f.song_source_ref_id
JOIN song_artist_refs ar ON ar.song_source_ref_id = r.id
JOIN singer_catalog_sources ssr ON ssr.id = ar.singer_source_ref_id
WHERE r.source = 'migu'
  AND ssr.source = r.source
  AND ssr.source_singer_id = ?
