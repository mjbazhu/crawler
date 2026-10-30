SELECT task_id,operation_id FROM resource_transfer_tasks WHERE destination=? AND state='running' AND claimed_at<=? ORDER BY task_id LIMIT 200
