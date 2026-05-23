SELECT
    s.id,
    s.title,
    s.normalized_title,
    r.source_duration_seconds,
    r.id,
    r.source,
    r.source_item_key,
    COALESCE(r.source_song_id, ''),
    COALESCE(r.source_content_id, ''),
    COALESCE(r.source_resource_type, ''),
    COALESCE(r.source_title_pinyin, ''),
    COALESCE(r.source_album_id, ''),
    COALESCE(r.source_album_name, ''),
    COALESCE((SELECT COALESCE(NULLIF(ar.source_artist_id, ''), sr.source_singer_id, '') FROM song_artist_refs ar LEFT JOIN singer_source_refs sr ON sr.id = ar.singer_source_ref_id WHERE ar.song_source_ref_id = r.id ORDER BY ar.is_seed DESC, ar.is_primary DESC, ar.artist_order ASC, ar.id ASC LIMIT 1), ''),
    COALESCE((SELECT COALESCE(NULLIF(ar.source_artist_name, ''), sra.name, '') FROM song_artist_refs ar LEFT JOIN singer_source_refs sr ON sr.id = ar.singer_source_ref_id LEFT JOIN singers sra ON sra.id = sr.singer_db_id WHERE ar.song_source_ref_id = r.id ORDER BY ar.is_seed DESC, ar.is_primary DESC, ar.artist_order ASC, ar.id ASC LIMIT 1), ''),
    COALESCE(r.cover_url, ''),
    COALESCE(r.lyric_url, ''),
    COALESCE(r.mrc_url, ''),
    COALESCE(r.trc_url, ''),
    COALESCE(r.copyright_id, ''),
    r.metadata_status,
    COALESCE(f.format_types, ''),
    COALESCE(res.resource_count, 0),
    COALESCE(res.failed_resource_count, 0),
    r.created_at,
    r.updated_at
FROM song_source_refs r
JOIN songs s ON s.id = r.song_db_id
LEFT JOIN (
    SELECT song_source_ref_id, GROUP_CONCAT(format_type ORDER BY format_type SEPARATOR ',') AS format_types
    FROM song_source_audio_formats
    GROUP BY song_source_ref_id
) f ON f.song_source_ref_id = r.id
LEFT JOIN (
    SELECT song_source_ref_id, COUNT(*) AS resource_count, SUM(CASE WHEN status = 3 THEN 1 ELSE 0 END) AS failed_resource_count
    FROM song_resources
    GROUP BY song_source_ref_id
) res ON res.song_source_ref_id = r.id
WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (? = '' OR s.title LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR r.source_song_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR r.source_content_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%'))
  AND (? = '' OR EXISTS (SELECT 1 FROM song_artist_refs ar WHERE ar.song_source_ref_id = r.id AND (ar.source_artist_name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR ar.source_artist_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci OR EXISTS (SELECT 1 FROM singer_source_refs sr JOIN singers sra ON sra.id = sr.singer_db_id WHERE sr.id = ar.singer_source_ref_id AND (sra.name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR sr.source_singer_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)))))
  AND (? = '' OR r.source_album_name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR r.source_album_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci OR EXISTS (SELECT 1 FROM album_song_refs abr WHERE abr.song_source_ref_id = r.id AND abr.source_album_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci))
  AND (? = '' OR EXISTS (SELECT 1 FROM song_artist_refs ar WHERE ar.song_source_ref_id = r.id AND (ar.source_artist_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci OR EXISTS (SELECT 1 FROM singer_source_refs sr WHERE sr.id = ar.singer_source_ref_id AND sr.source_singer_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci))))
  AND (? = '' OR r.source_album_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci OR EXISTS (SELECT 1 FROM album_song_refs abr WHERE abr.song_source_ref_id = r.id AND abr.source_album_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci))
  AND (? = 0 OR EXISTS (SELECT 1 FROM song_resources sr WHERE sr.song_source_ref_id = r.id AND sr.status IN (?, ?, ?, ?, ?)))
  AND (? = '' OR EXISTS (SELECT 1 FROM song_source_audio_formats af WHERE af.song_source_ref_id = r.id AND af.format_type = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci))
ORDER BY r.updated_at DESC, r.id DESC
LIMIT ? OFFSET ?
