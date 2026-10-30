UPDATE album_resources
SET content_generation=IF(reference_sha256<>'' AND (BINARY reference_sha256<>BINARY ? OR reference_size_bytes<>?),content_generation+1,content_generation),
    local_path=?,size_bytes=?,checksum=?,reference_sha256=?,reference_size_bytes=?,local_content_generation=content_generation,
    local_target_revision=(SELECT target_revision FROM resource_destination_states WHERE destination='local'),
    status=2,error_category='',result_code='ok',last_error=NULL,fetched_at=UNIX_TIMESTAMP(),claimed_at=0,running_batch_id=NULL,updated_at=UNIX_TIMESTAMP()
WHERE id=? AND status=1 AND claim_generation=? AND BINARY COALESCE(remote_url,'')=BINARY ?;
