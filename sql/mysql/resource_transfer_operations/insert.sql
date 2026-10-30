INSERT INTO resource_transfer_operations(operation_id,idempotency_key,request_hash,request_snapshot,action,logical_resource_count,task_count,created_at,updated_at)
VALUES (?,NULLIF(?,''),?,CAST(? AS JSON),?,?,0,UNIX_TIMESTAMP(),UNIX_TIMESTAMP()) ON DUPLICATE KEY UPDATE operation_id=operation_id
