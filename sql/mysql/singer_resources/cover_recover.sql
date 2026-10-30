UPDATE singer_resources r JOIN singer_source_refs s ON s.id=r.singer_source_ref_id
SET r.status=IF(NULLIF(r.local_path,'') IS NULL,3,2), r.claim_generation=r.claim_generation+1,
    r.error_category='timeout', r.result_code='cover_stale_recovered', r.last_error='stale cover claim recovered',
    r.claimed_at=0, r.running_batch_id=NULL, r.updated_at=UNIX_TIMESTAMP()
WHERE s.source=? AND r.status=1 AND r.claimed_at<=UNIX_TIMESTAMP()-?
  AND NOT (r.local_skip_generation=r.content_generation AND r.local_skip_target_revision=(SELECT target_revision FROM resource_destination_states WHERE destination='local') AND r.local_skip_baseline<>'' AND r.local_skip_reason<>'')
  AND NOT EXISTS (SELECT 1 FROM resource_baseline_blocks blocked WHERE blocked.owner_type='singer' AND blocked.resource_id=r.id AND blocked.content_generation=r.content_generation)
  AND NOT EXISTS (SELECT 1 FROM resource_transfer_slots slot WHERE slot.owner_type='singer' AND slot.resource_id=r.id)
  AND NOT EXISTS (SELECT 1 FROM resource_cloud_receipts receipt WHERE receipt.owner_type='singer' AND receipt.resource_id=r.id AND (receipt.task_state IN ('running','uncertain') OR receipt.attempt IS NOT NULL) AND COALESCE(JSON_UNQUOTE(JSON_EXTRACT(receipt.attempt,'$.legacy_cover')),'false')<>'true') /*__FILTERS__*/;
