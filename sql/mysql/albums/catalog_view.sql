CREATE OR REPLACE VIEW album_catalog AS
SELECT a.*,COALESCE((SELECT s.name FROM album_source_refs ar JOIN album_artist_refs aa ON aa.album_source_ref_id=ar.id JOIN singer_source_refs sr ON sr.id=aa.singer_source_ref_id JOIN singers s ON s.id=sr.singer_db_id WHERE ar.album_db_id=a.id ORDER BY aa.is_primary DESC,aa.artist_order,aa.id LIMIT 1),'') AS primary_artist_name
FROM albums a;
