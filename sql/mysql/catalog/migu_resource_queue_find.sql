SELECT
    sr.id,
    sr.song_source_ref_id,
    r.source,
    COALESCE(r.source_song_id, ''),
    COALESCE(r.source_content_id, ''),
    s.title,
    COALESCE((SELECT COALESCE(NULLIF(ar.source_artist_name, ''), sra.name, '') FROM song_artist_refs ar LEFT JOIN singer_source_refs ssr ON ssr.id = ar.singer_source_ref_id LEFT JOIN singers sra ON sra.id = ssr.singer_db_id WHERE ar.song_source_ref_id = r.id ORDER BY ar.is_seed DESC, ar.is_primary DESC, ar.artist_order ASC, ar.id ASC LIMIT 1), ''),
    COALESCE(r.source_album_name, ''),
    sr.resource_kind,
    sr.format_type,
    COALESCE(sr.remote_url, ''),
    COALESCE(sr.local_path, ''),
    sr.size_bytes,
    COALESCE(sr.checksum, ''),
    sr.status,
    sr.retry_count,
    COALESCE(sr.last_error, ''),
    COALESCE(sr.error_category, ''),
    COALESCE(sr.result_code, ''),
    COALESCE(sr.last_batch_id, ''),
    COALESCE(sr.running_batch_id, ''),
    sr.last_processed_at,
    sr.fetched_at,
    sr.created_at,
    sr.updated_at
FROM song_resources sr
JOIN song_source_refs r ON r.id = sr.song_source_ref_id
JOIN songs s ON s.id = r.song_db_id
WHERE sr.id = ?
