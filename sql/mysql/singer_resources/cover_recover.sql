UPDATE singer_resources r JOIN singer_source_refs s ON s.id=r.singer_source_ref_id
SET r.status=IF(NULLIF(r.local_path,'') IS NULL,3,2), r.claim_generation=r.claim_generation+1,
    r.error_category='timeout', r.result_code='cover_stale_recovered', r.last_error='stale cover claim recovered',
    r.claimed_at=0, r.running_batch_id=NULL, r.updated_at=UNIX_TIMESTAMP()
WHERE s.source=? AND r.status=1 AND r.claimed_at<=UNIX_TIMESTAMP()-? /*__FILTERS__*/;
