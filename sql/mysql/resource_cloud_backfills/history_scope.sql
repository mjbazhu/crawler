SELECT cloud_provider,strategy FROM resource_cloud_backfills WHERE run_id IN(?,?) ORDER BY cloud_provider FOR UPDATE;
