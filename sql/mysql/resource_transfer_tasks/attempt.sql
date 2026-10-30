UPDATE resource_transfer_tasks SET attempt_path=?,attempt_checksum=?,attempt_size_bytes=?,updated_at=UNIX_TIMESTAMP() WHERE task_id=? AND state='running' AND claim_generation=? AND cancel_requested=0
