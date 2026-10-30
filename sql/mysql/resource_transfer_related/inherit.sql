INSERT INTO resource_transfer_related(parent_task_id,discovered_mask,claim_generation,created_at,updated_at)
SELECT ?,discovered_mask,0,UNIX_TIMESTAMP(),UNIX_TIMESTAMP() FROM resource_transfer_related WHERE parent_task_id=? AND child_operation_id IS NULL
