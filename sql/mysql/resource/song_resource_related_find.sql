SELECT
    sr.id,
    sr.song_source_ref_id,
    r.source,
    COALESCE(r.source_song_id, ''),
    COALESCE(r.source_content_id, ''),
    COALESCE(r.source_resource_type, ''),
    '',
    COALESCE(r.copyright_id, ''),
    s.title,
    COALESCE((SELECT sra.name FROM song_artist_refs ar JOIN singer_source_refs ssr ON ssr.id = ar.singer_source_ref_id JOIN singers sra ON sra.id = ssr.singer_db_id WHERE ar.song_source_ref_id = r.id ORDER BY ar.is_seed DESC, ar.is_primary DESC, ar.artist_order ASC, ar.id ASC LIMIT 1), ''),
    COALESCE(r.source_album_name, ''),
    COALESCE(ar.source_album_kind, 0),
    sr.resource_kind,
    sr.format_type,
    COALESCE(sr.remote_url, ''),
    COALESCE(sr.local_path, ''),
    sr.status,
    sr.retry_count
FROM song_resources sr
JOIN song_source_refs r ON r.id = sr.song_source_ref_id
JOIN songs s ON s.id = r.song_db_id
LEFT JOIN album_source_refs ar ON ar.source = r.source
    AND ar.source_album_id = r.source_album_id
WHERE sr.song_source_ref_id = ?
  AND sr.resource_kind = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND sr.format_type = ''
LIMIT 1
