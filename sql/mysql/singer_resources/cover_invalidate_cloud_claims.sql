-- 本地提交仅废止依赖旧本地认领的legacy云意图，保留逐端确认和skip，不接管统一任务。
UPDATE resource_cloud_receipts c SET c.task_state='pending',c.claim_generation=c.claim_generation+1,c.claimed_at=0,c.running_batch_id='',c.attempt=NULL,
    c.error_category='',c.result_code='',c.last_error=NULL,c.updated_at=UNIX_TIMESTAMP()
WHERE c.owner_type='singer' AND c.resource_id=? AND c.task_state='running' AND JSON_EXTRACT(c.attempt,'$.legacy_cover')=CAST('true' AS JSON)
  AND NOT(c.transfer_policy='skip' AND c.policy_content_generation=c.content_generation AND c.policy_target_revision=c.target_revision)
  AND NOT EXISTS(SELECT 1 FROM resource_transfer_slots slot WHERE slot.owner_type='singer' AND slot.resource_id=c.resource_id AND slot.destination=c.cloud_provider);
