SELECT COALESCE(r.local_path,''), r.status, a.detail_synced_at > 0
FROM album_resources r
JOIN album_source_refs a ON a.id=r.album_source_ref_id
WHERE r.album_source_ref_id=? AND r.resource_kind='cover'
FOR UPDATE;
