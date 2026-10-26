CREATE OR REPLACE VIEW album_song_catalog AS
SELECT r.id,r.album_source_ref_id,r.song_source_ref_id,a.source,a.source_album_id,
       s.source_song_id,s.source_content_id,r.disc_no,r.disc_name,r.track_no,r.song_order,r.created_at,r.updated_at
FROM album_song_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id
JOIN song_source_refs s ON s.id=r.song_source_ref_id;
