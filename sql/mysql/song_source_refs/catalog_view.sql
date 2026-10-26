CREATE OR REPLACE VIEW song_catalog_sources AS
SELECT r.*,CASE WHEN NULLIF(TRIM(r.source_content_id),'') IS NOT NULL THEN CONCAT('content:',r.source_content_id) ELSE CONCAT('song:',r.source_song_id) END AS source_item_key,
       COALESCE((SELECT a.source_album_id FROM album_song_refs ar JOIN album_source_refs a ON a.id=ar.album_source_ref_id WHERE ar.song_source_ref_id=r.id ORDER BY ar.id LIMIT 1),'') AS source_album_id,
       COALESCE((SELECT title_row.title FROM album_song_refs ar JOIN album_source_refs a ON a.id=ar.album_source_ref_id JOIN albums title_row ON title_row.id=a.album_db_id WHERE ar.song_source_ref_id=r.id ORDER BY ar.id LIMIT 1),'') AS source_album_name
FROM song_source_refs r;
