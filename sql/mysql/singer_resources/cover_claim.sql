UPDATE singer_resources SET status=1, claim_generation=claim_generation+1,
    running_batch_id=NULLIF(?,''), claimed_at=UNIX_TIMESTAMP(),
    error_category='', result_code='', last_error=NULL, updated_at=UNIX_TIMESTAMP()
WHERE id=? AND claim_generation=? AND BINARY COALESCE(remote_url,'')=BINARY ?
  AND (status IN (0,3) OR (?=1 AND status=2))
  AND NOT EXISTS(SELECT 1 FROM resource_transfer_slots slot WHERE slot.owner_type='singer' AND slot.resource_id=singer_resources.id AND slot.destination='local')
  AND NOT(local_skip_generation=content_generation AND local_skip_target_revision=(SELECT target_revision FROM resource_destination_states WHERE destination='local') AND local_skip_baseline<>'' AND local_skip_reason<>'')
  AND NOT EXISTS(SELECT 1 FROM resource_baseline_blocks blocked WHERE blocked.owner_type='singer' AND blocked.resource_id=singer_resources.id AND blocked.content_generation=singer_resources.content_generation);
