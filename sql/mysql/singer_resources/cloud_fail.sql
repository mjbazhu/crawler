UPDATE resource_cloud_receipts c JOIN singer_resources r ON r.id=c.singer_resource_id
JOIN resource_destination_states d ON d.destination=c.cloud_provider
SET c.task_state=?,c.error_category=?,c.result_code=?,c.last_error=?,c.attempt=IF(?=1,c.attempt,NULL),c.claimed_at=0,c.running_batch_id='',c.updated_at=UNIX_TIMESTAMP()
WHERE c.owner_type='singer' AND c.cloud_provider=? AND r.id=? AND c.task_state='running' AND c.claim_generation=? AND c.running_batch_id=?
  AND r.content_generation=? AND r.source_revision=? AND c.content_generation=r.content_generation AND d.target_revision=? AND c.target_revision=d.target_revision
  AND JSON_EXTRACT(c.attempt,'$.legacy_cover')=CAST('true' AS JSON);
