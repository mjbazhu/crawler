INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,song_resource_id,singer_resource_id,album_resource_id,artifact_resource_id,created_at,updated_at)
SELECT owner_type,resource_id,destination,
    NULLIF(CASE WHEN owner_type='song' THEN resource_id ELSE 0 END,0),
    NULLIF(CASE WHEN owner_type='singer' THEN resource_id ELSE 0 END,0),
    NULLIF(CASE WHEN owner_type='album' THEN resource_id ELSE 0 END,0),
    NULLIF(CASE WHEN owner_type='song_artifact' THEN resource_id ELSE 0 END,0),
    UNIX_TIMESTAMP(),UNIX_TIMESTAMP()
FROM resource_transfer_tasks
WHERE operation_id=? AND state='pending' AND destination<>'local'
ORDER BY task_id
ON DUPLICATE KEY UPDATE id=id
