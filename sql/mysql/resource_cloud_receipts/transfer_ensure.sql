INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,song_resource_id,singer_resource_id,album_resource_id,artifact_resource_id,created_at,updated_at)
VALUES(?,?,?,NULLIF(?,0),NULLIF(?,0),NULLIF(?,0),NULLIF(?,0),UNIX_TIMESTAMP(),UNIX_TIMESTAMP()) ON DUPLICATE KEY UPDATE id=id
