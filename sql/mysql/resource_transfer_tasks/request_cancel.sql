UPDATE resource_transfer_tasks SET cancel_requested=1,updated_at=UNIX_TIMESTAMP() WHERE task_id=? AND state IN ('pending','retry','running')
