SELECT id,batch_id FROM resource_legacy_batches WHERE finished_at=0 AND id>? ORDER BY id LIMIT 1
