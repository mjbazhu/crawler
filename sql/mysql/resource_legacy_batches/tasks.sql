WITH RECURSIVE scope(operation_id,depth) AS (
 SELECT operation_id,0 FROM resource_legacy_operations WHERE batch_id=?
 UNION ALL
 SELECT c.operation_id,s.depth+1 FROM resource_transfer_operations c JOIN scope s ON c.parent_operation_id=s.operation_id WHERE s.depth<2
)
SELECT t.owner_type,t.resource_id,t.destination,t.state,t.result_code,t.error_category,
 COALESCE(t.attempt_path,''),t.attempt_checksum,t.attempt_size_bytes,s.depth,t.task_id
FROM resource_transfer_tasks t JOIN scope s ON s.operation_id=t.operation_id
ORDER BY s.depth,t.task_id
