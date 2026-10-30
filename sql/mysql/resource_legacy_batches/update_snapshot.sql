UPDATE resource_legacy_batches SET request_snapshot=CAST(? AS JSON) WHERE batch_id=? AND finished_at=0
