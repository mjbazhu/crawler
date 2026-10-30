SELECT producer_id,parent_operation_id,parent_task_id FROM resource_producer_tasks
WHERE destination=? AND state='running' AND claimed_at<=? ORDER BY producer_id LIMIT 200
