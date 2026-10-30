UPDATE resource_transfer_tasks SET state=?,result_code=?,error_category=?,available_at=?,updated_at=UNIX_TIMESTAMP() WHERE task_id=? AND claim_generation=?
