INSERT INTO resource_transfer_tasks(operation_id,owner_type,resource_id,destination,content_generation,source_revision,source_identity,target_revision,target_fingerprint,state,timeout_ms,retries,max_bytes,bytes_per_second,busy_task_id,result_code,attempt_path,attempt_checksum,attempt_size_bytes,available_at,created_at,updated_at)
SELECT ?,planned.owner_type,planned.resource_id,planned.destination,planned.content_generation,planned.source_revision,planned.source_identity,planned.target_revision,planned.target_fingerprint,planned.state,planned.timeout_ms,planned.retries,planned.max_bytes,planned.bytes_per_second,planned.busy_task_id,planned.result_code,NULLIF(planned.attempt_path,''),planned.attempt_checksum,planned.attempt_size_bytes,UNIX_TIMESTAMP(),UNIX_TIMESTAMP(),UNIX_TIMESTAMP()
FROM JSON_TABLE(CAST(? AS JSON),'$[*]' COLUMNS (
    ordinal FOR ORDINALITY,
    owner_type VARCHAR(16) PATH '$.owner_type' ERROR ON EMPTY ERROR ON ERROR,
    resource_id BIGINT UNSIGNED PATH '$.resource_id' ERROR ON EMPTY ERROR ON ERROR,
    destination VARCHAR(16) PATH '$.destination' ERROR ON EMPTY ERROR ON ERROR,
    content_generation BIGINT UNSIGNED PATH '$.content_generation' ERROR ON EMPTY ERROR ON ERROR,
    source_revision BIGINT UNSIGNED PATH '$.source_revision' ERROR ON EMPTY ERROR ON ERROR,
    source_identity CHAR(64) PATH '$.source_identity' ERROR ON EMPTY ERROR ON ERROR,
    target_revision BIGINT UNSIGNED PATH '$.target_revision' ERROR ON EMPTY ERROR ON ERROR,
    target_fingerprint CHAR(64) PATH '$.target_fingerprint' ERROR ON EMPTY ERROR ON ERROR,
    state VARCHAR(24) PATH '$.state' ERROR ON EMPTY ERROR ON ERROR,
    timeout_ms INT UNSIGNED PATH '$.timeout_ms' ERROR ON EMPTY ERROR ON ERROR,
    retries INT UNSIGNED PATH '$.retries' ERROR ON EMPTY ERROR ON ERROR,
    max_bytes BIGINT UNSIGNED PATH '$.max_bytes' ERROR ON EMPTY ERROR ON ERROR,
    bytes_per_second BIGINT UNSIGNED PATH '$.bytes_per_second' ERROR ON EMPTY ERROR ON ERROR,
    busy_task_id BIGINT UNSIGNED PATH '$.busy_task_id' ERROR ON EMPTY ERROR ON ERROR,
    result_code VARCHAR(64) PATH '$.result_code' ERROR ON EMPTY ERROR ON ERROR,
    attempt_path TEXT PATH '$.attempt_path' ERROR ON EMPTY ERROR ON ERROR,
    attempt_checksum CHAR(64) PATH '$.attempt_checksum' ERROR ON EMPTY ERROR ON ERROR,
    attempt_size_bytes BIGINT UNSIGNED PATH '$.attempt_size_bytes' ERROR ON EMPTY ERROR ON ERROR
)) AS planned
ORDER BY planned.ordinal
