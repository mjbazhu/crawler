-- 页内有界批量审计；旧值仍按完整JSON文本转换，不窄化身份/证据后静默截断。
INSERT INTO resource_cloud_migration_audit(run_id,owner_type,resource_id,source_identity,content_generation,old_values,disposition,created_at)
SELECT ?,?,items.resource_id,items.source_identity,items.content_generation,CAST(items.old_values AS JSON),items.disposition,UNIX_TIMESTAMP()
FROM JSON_TABLE(CAST(? AS JSON),'$[*]' COLUMNS(
    ordinal FOR ORDINALITY,
    resource_id BIGINT UNSIGNED PATH '$.resource_id' ERROR ON EMPTY ERROR ON ERROR,
    source_identity LONGTEXT PATH '$.source_identity' ERROR ON EMPTY ERROR ON ERROR,
    content_generation BIGINT UNSIGNED PATH '$.content_generation' ERROR ON EMPTY ERROR ON ERROR,
    old_values LONGTEXT PATH '$.old_values' ERROR ON EMPTY ERROR ON ERROR,
    disposition LONGTEXT PATH '$.disposition' ERROR ON EMPTY ERROR ON ERROR
)) AS items
ORDER BY items.ordinal
ON DUPLICATE KEY UPDATE run_id=resource_cloud_migration_audit.run_id;
