UPDATE album_resources SET cloud_backend=?, cloud_path=?, cloud_size_bytes=?, cloud_checksum=?,
    cloud_status=2, cloud_uploaded_at=UNIX_TIMESTAMP(), cloud_claimed_at=0, cloud_running_batch_id=NULL,
    cloud_error_category='', cloud_result_code='ok', cloud_last_error=NULL, cloud_attempt=NULL
WHERE id=? AND status=2 AND cloud_status=1 AND cloud_claim_generation=?
  AND BINARY COALESCE(local_path,'')=BINARY ? AND size_bytes=? AND BINARY COALESCE(checksum,'')=BINARY ?;
