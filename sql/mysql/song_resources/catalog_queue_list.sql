SELECT
    sr.id,
    sr.song_source_ref_id,
    r.source,
    COALESCE(r.source_song_id, ''),
    COALESCE(r.source_content_id, ''),
    s.title,
    COALESCE((SELECT sra.name FROM song_artist_refs ar JOIN singer_source_refs ssr ON ssr.id = ar.singer_source_ref_id JOIN singers sra ON sra.id = ssr.singer_db_id WHERE ar.song_source_ref_id = r.id ORDER BY ar.is_seed DESC, ar.is_primary DESC, ar.artist_order ASC, ar.id ASC LIMIT 1), ''),
    COALESCE((SELECT a2_title.title FROM album_song_refs ar2 JOIN album_source_refs a2 ON a2.id=ar2.album_source_ref_id JOIN albums a2_title ON a2_title.id=a2.album_db_id WHERE ar2.song_source_ref_id=r.id ORDER BY ar2.id LIMIT 1),''),
    sr.resource_kind,
    sr.format_type,
    '',
    COALESCE(sr.local_path, ''),
    COALESCE(sr.cloud_backend, ''),
    COALESCE(sr.cloud_path, ''),
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
    sr.cloud_uploaded_at,
    sr.created_at,
    sr.updated_at
FROM song_resources sr
JOIN (
    SELECT sr.id
    FROM song_resources sr
    STRAIGHT_JOIN song_source_refs r ON r.id = sr.song_source_ref_id
    WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
      AND (? = '' OR EXISTS (SELECT 1 FROM songs st WHERE st.id=r.song_db_id AND st.title LIKE CONCAT('%',CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,'%')) OR r.source_song_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR r.source_content_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%'))
      AND (? = 0 OR sr.status IN (?, ?, ?, ?, ?))
      AND (? = '' OR sr.resource_kind = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
      AND (? = '' OR sr.format_type = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
    ORDER BY sr.updated_at ASC, sr.id ASC
    LIMIT ? OFFSET ?
) page ON page.id = sr.id
JOIN song_source_refs r ON r.id = sr.song_source_ref_id
JOIN songs s ON s.id = r.song_db_id
ORDER BY sr.updated_at ASC, sr.id ASC
