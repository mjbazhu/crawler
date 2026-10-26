UPDATE crawl_batches
SET state = 'Failed',
    processed_count = target_count,
    failure_count = GREATEST(failure_count, GREATEST(target_count - success_count, 0)),
    last_error = IF(last_error = '', ?, last_error),
    finished_at = IF(finished_at = 0, ?, finished_at),
    updated_at = ?
WHERE batch_id = ?
  AND state = 'Running'
