SELECT COUNT(*)
FROM song_catalog_sources r
JOIN song_artist_refs ar ON ar.song_source_ref_id = r.id
JOIN singer_catalog_sources ssr ON ssr.id = ar.singer_source_ref_id
WHERE r.source = 'migu'
  AND ssr.source = r.source
  AND ssr.source_singer_id = ?
