UPDATE resource_transfer_related SET child_operation_id=?,updated_at=UNIX_TIMESTAMP() WHERE parent_task_id=? AND child_operation_id IS NULL
