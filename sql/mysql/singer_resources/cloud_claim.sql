UPDATE resource_cloud_receipts c JOIN singer_resources r ON r.id=c.singer_resource_id
JOIN resource_destination_states d ON d.destination=c.cloud_provider
SET c.task_state='running',c.cancel_requested=0,c.next_attempt_at=0,c.attempt_count=c.attempt_count+1,c.claim_generation=c.claim_generation+1,c.running_batch_id=?,c.claimed_at=UNIX_TIMESTAMP(),c.attempt=CAST(? AS JSON),
    c.content_generation=r.content_generation,c.target_revision=d.target_revision,c.error_category='',c.result_code='',c.last_error=NULL,c.updated_at=UNIX_TIMESTAMP()
WHERE c.owner_type='singer' AND c.cloud_provider=? AND r.id=? AND r.status=2 AND c.task_state<>'running' AND c.claim_generation=?
  AND r.content_generation=? AND r.source_revision=? AND d.target_revision=? AND r.claim_generation=?
  AND BINARY COALESCE(r.local_path,'')=BINARY ? AND r.size_bytes=? AND BINARY COALESCE(r.checksum,'')=BINARY ?
  AND c.verification<>'sha256_verified'
  AND (c.confirmed_content_generation=0 OR (c.verification='hash_matched' AND c.verification_method='legacy_cover_store'))
  AND NOT(c.transfer_policy='skip' AND c.policy_content_generation=r.content_generation AND c.policy_target_revision=d.target_revision)
  AND NOT EXISTS(SELECT 1 FROM resource_transfer_slots slot WHERE slot.owner_type='singer' AND slot.resource_id=r.id AND slot.destination=c.cloud_provider)
  AND (c.attempt IS NULL OR (JSON_EXTRACT(c.attempt,'$.legacy_cover')=CAST('true' AS JSON)
    AND BINARY JSON_UNQUOTE(JSON_EXTRACT(c.attempt,'$.cloud_path'))=BINARY ?
    AND BINARY JSON_UNQUOTE(JSON_EXTRACT(c.attempt,'$.backend'))=BINARY ?
    AND BINARY JSON_UNQUOTE(JSON_EXTRACT(c.attempt,'$.local_path'))=BINARY COALESCE(r.local_path,'')
    AND JSON_EXTRACT(c.attempt,'$.size_bytes')=r.size_bytes
    AND BINARY JSON_UNQUOTE(JSON_EXTRACT(c.attempt,'$.checksum'))=BINARY COALESCE(r.checksum,'')))
  AND (c.task_state<>'uncertain' OR JSON_EXTRACT(c.attempt,'$.legacy_cover')=CAST('true' AS JSON));
