INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,album_resource_id,content_generation,target_revision,created_at,updated_at)
SELECT 'album',r.id,d.destination,r.id,r.content_generation,d.target_revision,UNIX_TIMESTAMP(),UNIX_TIMESTAMP()
FROM album_resources r JOIN resource_destination_states d ON d.destination=? WHERE r.id=?
ON DUPLICATE KEY UPDATE id=resource_cloud_receipts.id;
