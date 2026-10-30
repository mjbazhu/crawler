SELECT manifest_hash,database_identity,owner_type,cloud_provider,target_revision,high_water_id,last_committed_id,state,processed_count,imported_count,conflict_count,control_state,control_generation,strategy
FROM resource_cloud_backfills WHERE run_id IN (?,?) ORDER BY cloud_provider;
