WITH page AS (
    SELECT r.id,r.updated_at
    FROM song_source_refs r
    WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
      AND (r.source_song_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci OR r.source_content_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
    LIMIT 1
)
SELECT
    s.id,
    s.title,
    s.normalized_title,
    r.source_duration_seconds,
    r.id,
    r.source,
    CASE WHEN NULLIF(TRIM(r.source_content_id),'') IS NOT NULL THEN CONCAT('content:',r.source_content_id) ELSE CONCAT('song:',r.source_song_id) END AS source_item_key,
    COALESCE(r.source_song_id, ''),
    COALESCE(r.source_content_id, ''),
    COALESCE(r.source_resource_type, ''),
    COALESCE(r.source_title_pinyin, ''),
    COALESCE((SELECT a2.source_album_id FROM album_song_refs ar2 JOIN album_source_refs a2 ON a2.id=ar2.album_source_ref_id WHERE ar2.song_source_ref_id=r.id ORDER BY ar2.id LIMIT 1),''),
    COALESCE((SELECT a2_title.title FROM album_song_refs ar2 JOIN album_source_refs a2 ON a2.id=ar2.album_source_ref_id JOIN albums a2_title ON a2_title.id=a2.album_db_id WHERE ar2.song_source_ref_id=r.id ORDER BY ar2.id LIMIT 1),''),
    COALESCE((SELECT sr.source_singer_id FROM song_artist_refs ar JOIN singer_source_refs sr ON sr.id = ar.singer_source_ref_id WHERE ar.song_source_ref_id = r.id ORDER BY ar.is_seed DESC, ar.is_primary DESC, ar.artist_order ASC, ar.id ASC LIMIT 1), ''),
    COALESCE((SELECT sra.name FROM song_artist_refs ar JOIN singer_source_refs sr ON sr.id = ar.singer_source_ref_id JOIN singers sra ON sra.id = sr.singer_db_id WHERE ar.song_source_ref_id = r.id ORDER BY ar.is_seed DESC, ar.is_primary DESC, ar.artist_order ASC, ar.id ASC LIMIT 1), ''),
    '',
    '',
    '',
    '',
    COALESCE(r.copyright_id, ''),
    r.metadata_status,
    COALESCE((SELECT GROUP_CONCAT(af.format_type ORDER BY af.format_type SEPARATOR ',') FROM song_source_audio_formats af WHERE af.song_source_ref_id=r.id),''),
    (SELECT COUNT(*) FROM song_resources rs WHERE rs.song_source_ref_id=r.id),
    (SELECT COUNT(*) FROM song_resources rs WHERE rs.song_source_ref_id=r.id AND rs.status=3),
    r.created_at,
    r.updated_at
FROM page p
JOIN song_source_refs r ON r.id=p.id
JOIN songs s ON s.id=r.song_db_id
ORDER BY p.updated_at DESC,p.id DESC
