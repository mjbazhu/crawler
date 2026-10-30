UPDATE album_resources SET claim_generation=claim_generation+1,running_batch_id=?,status=1,claimed_at=UNIX_TIMESTAMP(),updated_at=UNIX_TIMESTAMP() WHERE id=? AND status<>1
