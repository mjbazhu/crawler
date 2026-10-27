SELECT COALESCE(local_path,''), status, 0
FROM singer_resources
WHERE singer_source_ref_id=? AND resource_kind='cover'
FOR UPDATE;
