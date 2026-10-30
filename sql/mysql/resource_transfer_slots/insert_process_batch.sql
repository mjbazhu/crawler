INSERT INTO resource_transfer_slots(owner_type,resource_id,destination,task_id)
SELECT owner_type,resource_id,destination,task_id FROM resource_transfer_tasks
WHERE operation_id=? AND state='pending'
ORDER BY task_id
