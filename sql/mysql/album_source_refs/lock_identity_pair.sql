SELECT a.id, a.album_db_id, a.source_album_id,
       EXISTS(SELECT 1 FROM migu_dalbum_details d WHERE d.album_source_ref_id = a.id)
FROM album_source_refs a
WHERE a.source = 'migu' AND a.source_album_id IN (?, ?)
ORDER BY a.id
FOR UPDATE;
