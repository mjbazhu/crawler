UPDATE singer_resources SET cloud_status=?, cloud_error_category=?, cloud_result_code=?, cloud_last_error=?,
    cloud_claimed_at=0, cloud_running_batch_id=NULL
WHERE id=? AND cloud_status=1 AND cloud_claim_generation=?;
