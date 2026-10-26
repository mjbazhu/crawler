-- 关系筛选独立读取平台参数，使匹配主键集合只计算一次。
WITH catalog_source AS (
    SELECT CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci AS source
)
SELECT COUNT(*)
FROM song_source_refs r
JOIN catalog_source c ON c.source=r.source
WHERE (? = '' OR EXISTS (SELECT 1 FROM songs st WHERE st.id=r.song_db_id AND st.title LIKE CONCAT('%',CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,'%')) OR r.source_song_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR r.source_content_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%'))
  AND (? = '' OR EXISTS (SELECT 1 FROM song_artist_refs ar JOIN singer_source_refs sr ON sr.id = ar.singer_source_ref_id JOIN singers sra ON sra.id = sr.singer_db_id WHERE ar.song_source_ref_id = r.id AND (sra.name LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR sr.source_singer_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)))
  AND (? = '' OR EXISTS (SELECT 1 FROM album_song_refs ar JOIN album_source_refs al ON al.id=ar.album_source_ref_id JOIN albums al_title ON al_title.id=al.album_db_id WHERE ar.song_source_ref_id=r.id AND al.source=r.source AND (al_title.title LIKE CONCAT('%',CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,'%') OR al.source_album_id IN (CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci))))
  AND (? = '' OR r.id IN (SELECT ar.song_source_ref_id FROM singer_source_refs sr JOIN song_artist_refs ar ON ar.singer_source_ref_id=sr.id WHERE sr.source=(SELECT source FROM catalog_source) AND sr.source_singer_id=CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci))
  AND (? = '' OR r.id IN (SELECT ar.song_source_ref_id FROM album_source_refs al JOIN album_song_refs ar ON ar.album_source_ref_id=al.id WHERE al.source=(SELECT source FROM catalog_source) AND al.source_album_id IN (CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)))
  AND (? = 0 OR EXISTS (SELECT 1 FROM song_resources sr WHERE sr.song_source_ref_id = r.id AND sr.status IN (?, ?, ?, ?, ?)))
  AND (? = '' OR EXISTS (SELECT 1 FROM song_source_audio_formats af WHERE af.song_source_ref_id = r.id AND af.format_type = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci))
