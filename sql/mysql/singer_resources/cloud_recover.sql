UPDATE singer_resources r JOIN singer_source_refs s ON s.id=r.singer_source_ref_id
SET r.cloud_status=4, r.cloud_claim_generation=r.cloud_claim_generation+1,
    r.cloud_error_category='timeout', r.cloud_result_code='cloud_stale_recovered',
    r.cloud_last_error='stale cloud claim requires verification', r.cloud_claimed_at=0, r.cloud_running_batch_id=NULL
WHERE s.source=? AND r.cloud_status=1 AND r.cloud_claimed_at<=UNIX_TIMESTAMP()-? /*__FILTERS__*/;
