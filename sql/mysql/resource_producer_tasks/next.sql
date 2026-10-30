SELECT producer_id,parent_operation_id,parent_task_id FROM resource_producer_tasks
WHERE destination=? AND (state IN ('pending','retry') OR (?=1 AND state='waiting_input')) AND available_at<=UNIX_TIMESTAMP()
ORDER BY available_at,producer_id LIMIT 1
