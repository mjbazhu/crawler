SELECT COALESCE(local_path,'')
FROM album_resources
WHERE album_source_ref_id=? AND resource_kind='cover'
FOR UPDATE;
