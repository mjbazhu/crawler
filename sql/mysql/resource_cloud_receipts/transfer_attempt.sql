UPDATE resource_cloud_receipts SET attempt=JSON_OBJECT('path',?,'checksum',?,'size_bytes',?,'content_generation',?,'target_revision',?,'task_id',?,'claim_generation',?),updated_at=UNIX_TIMESTAMP()
WHERE owner_type=? AND resource_id=? AND cloud_provider=? AND claim_generation=? AND running_batch_id=?
