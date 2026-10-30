UPDATE resource_transfer_tasks SET attempt_path=NULL,attempt_checksum='',attempt_size_bytes=0 WHERE task_id=? AND claim_generation=?
