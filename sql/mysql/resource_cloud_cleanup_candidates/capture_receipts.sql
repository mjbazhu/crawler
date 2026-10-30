SELECT id,cloud_provider,confirmed_content_generation,confirmed_target_revision,COALESCE(cloud_path,''),size_bytes,checksum,verification,source_adapter,CASE WHEN attempt IS NOT NULL OR running_batch_id<>'' OR task_state IN('running','uncertain') THEN 'uncertain' ELSE task_state END,path_unresolved
FROM resource_cloud_receipts WHERE owner_type=? AND resource_id=? ORDER BY cloud_provider FOR UPDATE
