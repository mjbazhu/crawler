UPDATE resource_cloud_receipts c JOIN singer_resources r ON r.id=c.singer_resource_id
JOIN resource_destination_states d ON d.destination=c.cloud_provider
SET c.source_adapter=?,c.cloud_path=?,c.size_bytes=?,c.checksum=?,c.task_state='succeeded',c.uploaded_at=UNIX_TIMESTAMP(),c.verified_at=UNIX_TIMESTAMP(),
    c.confirmed_content_generation=r.content_generation,c.confirmed_target_revision=d.target_revision,c.verification='hash_matched',c.verification_method='legacy_cover_store',c.remote_hash_algorithm='',c.remote_hash_value='',
    r.reference_sha256=?,r.reference_size_bytes=?,
    r.local_content_generation=r.content_generation,
    c.path_unresolved=0,c.claimed_at=0,c.running_batch_id='',c.error_category='',c.result_code='ok',c.last_error=NULL,c.attempt=NULL,c.updated_at=UNIX_TIMESTAMP()
WHERE c.owner_type='singer' AND c.cloud_provider=? AND r.id=? AND r.status=2 AND c.task_state='running' AND c.claim_generation=? AND c.running_batch_id=?
  AND r.content_generation=? AND r.source_revision=? AND c.content_generation=r.content_generation AND d.target_revision=? AND c.target_revision=d.target_revision AND r.claim_generation=?
  AND BINARY COALESCE(r.local_path,'')=BINARY ? AND r.size_bytes=? AND BINARY COALESCE(r.checksum,'')=BINARY ?
  AND c.verification<>'sha256_verified'
  AND (c.confirmed_content_generation=0 OR (c.verification='hash_matched' AND c.verification_method='legacy_cover_store'))
  AND JSON_EXTRACT(c.attempt,'$.legacy_cover')=CAST('true' AS JSON)
  AND BINARY JSON_UNQUOTE(JSON_EXTRACT(c.attempt,'$.cloud_path'))=BINARY ?
  AND BINARY JSON_UNQUOTE(JSON_EXTRACT(c.attempt,'$.backend'))=BINARY ?
  AND (r.reference_sha256='' OR (BINARY r.reference_sha256=BINARY r.checksum AND r.reference_size_bytes=r.size_bytes));
