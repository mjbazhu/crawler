SELECT operation_id,state,logical_resource_count,action,request_hash,
 COALESCE(JSON_UNQUOTE(JSON_EXTRACT(request_snapshot,'$.parent_operation_id')),''),
 CAST(COALESCE(JSON_UNQUOTE(JSON_EXTRACT(request_snapshot,'$.parent_task_id')),'0') AS UNSIGNED)
FROM resource_transfer_operations WHERE operation_id=?
