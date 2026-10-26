-- 关系筛选独立读取平台参数，使匹配主键集合只计算一次。
WITH catalog_source AS (
    SELECT CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci AS source
),
page AS (
    SELECT r.id,r.updated_at
    FROM song_source_refs r
    JOIN catalog_source c ON c.source=r.source
    WHERE (? = '' OR EXISTS (SELECT 1 FROM songs st WHERE st.id=r.song_db_id AND st.title LIKE CONCAT('%',CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,'%')) OR r.source_song_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR r.source_content_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%'))
      AND (? = '' OR EXISTS (SELECT 1 FROM song_artist_refs ar JOIN singer_source_refs sr ON sr.id = ar.singer_source_ref_id JOIN singers sra ON sra.id = sr.singer_db_id WHERE ar.song_source_ref_id = r.id AND (sra.name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR sr.source_singer_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)))
      AND (? = '' OR EXISTS (SELECT 1 FROM album_song_refs ar JOIN album_source_refs al ON al.id=ar.album_source_ref_id JOIN albums al_title ON al_title.id=al.album_db_id WHERE ar.song_source_ref_id=r.id AND al.source=r.source AND (al_title.title LIKE CONCAT('%',CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,'%') OR al.source_album_id IN (CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci))))
      AND (? = '' OR r.id IN (SELECT ar.song_source_ref_id FROM singer_source_refs sr JOIN song_artist_refs ar ON ar.singer_source_ref_id=sr.id WHERE sr.source=(SELECT source FROM catalog_source) AND sr.source_singer_id=CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci))
      AND (? = '' OR r.id IN (SELECT ar.song_source_ref_id FROM album_source_refs al JOIN album_song_refs ar ON ar.album_source_ref_id=al.id WHERE al.source=(SELECT source FROM catalog_source) AND al.source_album_id IN (CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)))
      AND (? = 0 OR EXISTS (SELECT 1 FROM song_resources sr WHERE sr.song_source_ref_id = r.id AND sr.status IN (?, ?, ?, ?, ?)))
      AND (? = '' OR EXISTS (SELECT 1 FROM song_source_audio_formats af WHERE af.song_source_ref_id = r.id AND af.format_type = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci))
    ORDER BY r.updated_at DESC,r.id DESC
    LIMIT ? OFFSET ?
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
