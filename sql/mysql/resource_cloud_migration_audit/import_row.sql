INSERT INTO resource_cloud_migration_audit(run_id,owner_type,resource_id,source_identity,content_generation,old_values,disposition,created_at)
VALUES(?,?,?,?,?,CAST(? AS JSON),?,UNIX_TIMESTAMP())
ON DUPLICATE KEY UPDATE run_id=resource_cloud_migration_audit.run_id;
