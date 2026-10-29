UPDATE album_resources SET cloud_status=1, cloud_claim_generation=cloud_claim_generation+1,
    cloud_running_batch_id=NULLIF(?,''), cloud_claimed_at=UNIX_TIMESTAMP(), cloud_attempt=?,
    cloud_error_category='', cloud_result_code='', cloud_last_error=NULL
WHERE id=? AND status=2 AND cloud_status<>1 AND cloud_claim_generation=?
  AND BINARY COALESCE(local_path,'')=BINARY ? AND size_bytes=? AND BINARY COALESCE(checksum,'')=BINARY ?;
