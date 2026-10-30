UPDATE resource_legacy_batches SET terminal_result=CAST(? AS JSON),finished_at=UNIX_TIMESTAMP()
WHERE batch_id=? AND finished_at=0
