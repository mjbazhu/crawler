SELECT r.id,r.song_source_ref_id,s.source,COALESCE(s.source_song_id,''),COALESCE(s.source_content_id,''),m.title,
 r.resource_kind,r.format_type,COALESCE(r.local_path,''),r.size_bytes,COALESCE(r.checksum,''),r.status
FROM song_resources r JOIN song_source_refs s ON s.id=r.song_source_ref_id JOIN songs m ON m.id=s.song_db_id
WHERE s.source=? AND (?='' OR r.resource_kind=?) AND (?='' OR s.source_content_id=?) AND (?='' OR r.format_type=?)
 /*__FILTERS__*/
ORDER BY /*__ORDER__*/ r.id LIMIT ? OFFSET ?
