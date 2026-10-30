INSERT INTO resource_legacy_batches(batch_id,owner_type,cloud_provider,concurrency_limit,request_snapshot,created_at)
VALUES(?,?,?,?,CAST(? AS JSON),UNIX_TIMESTAMP())
