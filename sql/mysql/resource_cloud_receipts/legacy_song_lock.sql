SELECT task_state,transfer_policy,policy_content_generation,policy_target_revision,
 confirmed_content_generation,attempt IS NOT NULL,running_batch_id,
 source_adapter,COALESCE(cloud_path,''),size_bytes,
 COALESCE(JSON_UNQUOTE(JSON_EXTRACT(evidence,'$.legacy_song_writeback.claim_generation')),''),
 COALESCE(JSON_UNQUOTE(JSON_EXTRACT(evidence,'$.legacy_song_writeback.local_path')),''),
 COALESCE(JSON_UNQUOTE(JSON_EXTRACT(evidence,'$.legacy_song_writeback.checksum')),''),verification,
 content_generation,target_revision
FROM resource_cloud_receipts WHERE owner_type='song' AND resource_id=? AND cloud_provider=? FOR UPDATE;
