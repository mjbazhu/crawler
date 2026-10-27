UPDATE crawl_batches
SET state = IF(cancel_requested = 1, 'Cancelled', IF(? = '', state, ?)),
    target_count = IF(? = 0, target_count, ?),
    processed_count = ?,
    success_count = ?,
    failure_count = ?,
    cancel_requested = GREATEST(cancel_requested, ?),
    last_error = IF(? = '', last_error, ?),
    finished_at = IF(? = 0, finished_at, ?),
    updated_at = ?
WHERE batch_id = ?
