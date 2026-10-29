UPDATE singer_resources SET status=IF(NULLIF(local_path,'') IS NULL,3,2),
    error_category=?, result_code=?, last_error=?,
    claimed_at=0, running_batch_id=NULL, updated_at=UNIX_TIMESTAMP()
WHERE id=? AND status=1 AND claim_generation=?;
