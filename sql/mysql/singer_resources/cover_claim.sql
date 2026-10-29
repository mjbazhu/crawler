UPDATE singer_resources SET status=1, claim_generation=claim_generation+1,
    running_batch_id=NULLIF(?,''), claimed_at=UNIX_TIMESTAMP(),
    error_category='', result_code='', last_error=NULL, updated_at=UNIX_TIMESTAMP()
WHERE id=? AND claim_generation=? AND BINARY COALESCE(remote_url,'')=BINARY ?
  AND (status IN (0,3) OR (?=1 AND status=2));
