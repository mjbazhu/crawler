INSERT INTO governance_events(source,action,target_type,target_id,before_status,after_status,result_code,payload,created_at)
SELECT 'migu','resource.baseline.maintenance','baseline',baseline_id,'active','maintenance','ok',
 JSON_OBJECT('baseline_sha256',manifest_hash,'previous_activation',activated_at,'targets',CAST(? AS JSON)),UNIX_TIMESTAMP()
FROM resource_cloud_baselines WHERE baseline_id=?
