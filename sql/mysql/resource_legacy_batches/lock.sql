SELECT l.id,l.owner_type,l.cloud_provider,CAST(l.request_snapshot AS CHAR),
 CAST(l.terminal_result AS CHAR),b.state,b.created_at,b.finished_at,b.target_count,b.processed_count,b.success_count,b.failure_count
FROM resource_legacy_batches l JOIN crawl_batches b ON b.batch_id=l.batch_id WHERE l.batch_id=? FOR UPDATE
