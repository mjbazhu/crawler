CREATE OR REPLACE VIEW album_catalog_sources AS
SELECT r.*,CONCAT('album:',r.source_album_id) AS source_item_key,a.title AS source_title,
       COALESCE((SELECT s.source_singer_id FROM album_artist_refs ar JOIN singer_source_refs s ON s.id=ar.singer_source_ref_id WHERE ar.album_source_ref_id=r.id ORDER BY ar.is_primary DESC,ar.is_seed DESC,ar.artist_order,ar.id LIMIT 1),'') AS source_artist_id,
       COALESCE((SELECT s.name FROM album_artist_refs ar JOIN singer_source_refs sr ON sr.id=ar.singer_source_ref_id JOIN singers s ON s.id=sr.singer_db_id WHERE ar.album_source_ref_id=r.id ORDER BY ar.is_primary DESC,ar.is_seed DESC,ar.artist_order,ar.id LIMIT 1),'') AS source_artist_name,
       CASE WHEN r.source_resource_type='5' THEN '1' ELSE '0' END AS source_res_type,
       COALESCE(CAST(r.source_song_total AS CHAR),'') AS total_count,
       a.release_date AS release_date,a.release_date AS publish_date,0 AS play_num,'' AS play_num_desc
FROM album_source_refs r JOIN albums a ON a.id=r.album_db_id;
