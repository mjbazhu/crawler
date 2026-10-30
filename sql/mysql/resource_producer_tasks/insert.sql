INSERT INTO resource_producer_tasks(parent_operation_id,parent_task_id,destination,processor_version,schema_version,state,timeout_ms,retries,max_bytes,bytes_per_second,result_code,available_at,created_at,updated_at)
VALUES(?,?,?,?,?,?,?,?,?,?,?,UNIX_TIMESTAMP(),UNIX_TIMESTAMP(),UNIX_TIMESTAMP())
ON DUPLICATE KEY UPDATE producer_id=resource_producer_tasks.producer_id
