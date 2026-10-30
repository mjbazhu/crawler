UPDATE resource_cloud_baselines SET state='active',activated_at=COALESCE(activated_at,UNIX_TIMESTAMP()) WHERE baseline_id=? AND manifest_hash=?
