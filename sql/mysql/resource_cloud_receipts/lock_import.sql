SELECT cloud_provider,task_state,content_generation,target_revision
FROM resource_cloud_receipts WHERE owner_type=? AND resource_id=? ORDER BY cloud_provider FOR UPDATE;
