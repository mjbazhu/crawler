-- 历史云证据来自不可变收缩档案；新回执变化不改变旧清单，本地及来源仍实时读取。
SELECT r.id,r.content_generation,IF(s.id IS NULL,'',SHA2(CAST(JSON_ARRAY('singer',r.id,s.source,s.source_singer_id,r.resource_kind,SHA2(COALESCE(r.remote_url,''),256)) AS CHAR),256)),
       SHA2(CAST(JSON_OBJECT('local_path',COALESCE(r.local_path,''),'size_bytes',r.size_bytes,'checksum',COALESCE(r.checksum,''),'status',r.status,'cloud_backend',COALESCE(legacy.cloud_backend,''),'cloud_path',COALESCE(legacy.cloud_path,''),'cloud_uploaded_at',COALESCE(legacy.cloud_uploaded_at,0),'cloud_size_bytes',COALESCE(legacy.cloud_size_bytes,0),'cloud_checksum',COALESCE(legacy.cloud_checksum,''),'cloud_status',COALESCE(legacy.cloud_status,0)) AS CHAR),256),r.status,
       COALESCE(legacy.cloud_backend,''),COALESCE(legacy.cloud_path,''),COALESCE(r.local_path,''),r.size_bytes,COALESCE(r.checksum,''),
       JSON_OBJECT('local_path',COALESCE(r.local_path,''),'size_bytes',r.size_bytes,'checksum',COALESCE(r.checksum,''),'status',r.status,'cloud_backend',COALESCE(legacy.cloud_backend,''),'cloud_path',COALESCE(legacy.cloud_path,''),'cloud_uploaded_at',COALESCE(legacy.cloud_uploaded_at,0),'cloud_size_bytes',COALESCE(legacy.cloud_size_bytes,0),'cloud_checksum',COALESCE(legacy.cloud_checksum,''),'cloud_status',COALESCE(legacy.cloud_status,0))
FROM singer_resources r LEFT JOIN singer_source_refs s ON r.singer_source_ref_id=s.id
LEFT JOIN resource_cloud_migration_audit retired ON retired.run_id='schema-retirement-v2026102913' AND retired.owner_type='singer' AND retired.resource_id=r.id AND retired.disposition='archived'
LEFT JOIN JSON_TABLE(COALESCE(retired.old_values,JSON_OBJECT()),'$' COLUMNS(
    cloud_backend LONGTEXT PATH '$.cloud_backend' NULL ON EMPTY ERROR ON ERROR,
    cloud_path LONGTEXT PATH '$.cloud_path' NULL ON EMPTY ERROR ON ERROR,
    cloud_uploaded_at BIGINT UNSIGNED PATH '$.cloud_uploaded_at' NULL ON EMPTY ERROR ON ERROR,
    cloud_size_bytes BIGINT UNSIGNED PATH '$.cloud_size_bytes' NULL ON EMPTY ERROR ON ERROR,
    cloud_checksum LONGTEXT PATH '$.cloud_checksum' NULL ON EMPTY ERROR ON ERROR,
    cloud_status TINYINT PATH '$.cloud_status' NULL ON EMPTY ERROR ON ERROR
)) legacy ON TRUE
WHERE r.id>? AND r.id<=? ORDER BY r.id LIMIT ?
