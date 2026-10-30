SELECT resource_id,content_generation,source_identity,SHA2(CAST(old_values AS CHAR),256),disposition
FROM resource_cloud_migration_audit WHERE run_id=? AND owner_type=? AND resource_id>? AND resource_id<=? ORDER BY resource_id
