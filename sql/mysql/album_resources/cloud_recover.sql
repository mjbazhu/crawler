UPDATE resource_cloud_receipts c JOIN album_resources r ON r.id=c.album_resource_id JOIN album_source_refs s ON s.id=r.album_source_ref_id
SET c.task_state='uncertain',c.claim_generation=c.claim_generation+1,c.error_category='timeout',c.result_code='cloud_stale_recovered',
    c.last_error='stale cloud claim requires verification',c.claimed_at=0,c.running_batch_id='',c.updated_at=UNIX_TIMESTAMP()
WHERE c.owner_type='album' AND c.cloud_provider=? AND s.source=? AND c.task_state='running' AND c.claimed_at<=UNIX_TIMESTAMP()-?
  AND JSON_EXTRACT(c.attempt,'$.legacy_cover')=CAST('true' AS JSON)
  AND NOT(c.transfer_policy='skip' AND c.policy_content_generation=r.content_generation AND c.policy_target_revision=c.target_revision)
  AND NOT EXISTS(SELECT 1 FROM resource_transfer_slots slot WHERE slot.owner_type='album' AND slot.resource_id=r.id AND slot.destination=c.cloud_provider)
  AND NOT EXISTS(SELECT 1 FROM resource_baseline_blocks blocked WHERE blocked.owner_type='album' AND blocked.resource_id=r.id AND blocked.content_generation=r.content_generation) /*__FILTERS__*/;
