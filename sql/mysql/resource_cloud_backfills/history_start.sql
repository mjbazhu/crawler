INSERT INTO resource_cloud_backfills(run_id,manifest_hash,database_identity,owner_type,cloud_provider,target_revision,high_water_id,strategy,state,created_at,updated_at)
VALUES(?,?,?,?,?,?,?,'import_discovery_skip','paused',UNIX_TIMESTAMP(),UNIX_TIMESTAMP())
ON DUPLICATE KEY UPDATE run_id=resource_cloud_backfills.run_id;
