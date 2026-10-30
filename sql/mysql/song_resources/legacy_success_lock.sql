SELECT status,claim_generation,COALESCE(local_path,''),size_bytes,COALESCE(checksum,''),content_generation,
 local_skip_generation,local_skip_target_revision,local_skip_baseline,local_skip_reason,
 reference_sha256,reference_size_bytes,
 EXISTS(SELECT 1 FROM resource_baseline_blocks blocked WHERE blocked.owner_type='song' AND blocked.resource_id=song_resources.id AND blocked.content_generation=song_resources.content_generation)
FROM song_resources WHERE id=? FOR UPDATE;
