UPDATE album_resources SET local_path=?, size_bytes=?, checksum=?, status=2,
    error_category='', result_code='ok', last_error=NULL,
    cloud_status=0, cloud_claim_generation=cloud_claim_generation+1, cloud_claimed_at=0, cloud_running_batch_id=NULL,
    cloud_error_category='', cloud_result_code='', cloud_last_error=NULL, cloud_attempt=NULL,
    fetched_at=UNIX_TIMESTAMP(), claimed_at=0, running_batch_id=NULL, updated_at=UNIX_TIMESTAMP()
WHERE id=? AND status=1 AND claim_generation=? AND BINARY COALESCE(remote_url,'')=BINARY ?;
