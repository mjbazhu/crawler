UPDATE resource_transfer_tasks SET claim_generation=claim_generation+1,updated_at=UNIX_TIMESTAMP() WHERE task_id=? AND claim_generation=?
