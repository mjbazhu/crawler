SELECT task_state,claim_generation,running_batch_id,confirmed_content_generation,confirmed_target_revision,checksum,size_bytes,COALESCE(cloud_path,''),transfer_policy,policy_content_generation,policy_target_revision,baseline_id,skip_reason,verification
,source_adapter
FROM resource_cloud_receipts WHERE owner_type=? AND resource_id=? AND cloud_provider=? FOR UPDATE
