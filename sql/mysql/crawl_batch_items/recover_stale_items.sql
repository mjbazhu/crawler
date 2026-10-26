UPDATE crawl_batch_items
SET state = 'Failed',
    error_category = 'timeout',
    result_code = 'timeout',
    finished_at = IF(finished_at = 0, ?, finished_at),
    last_error = IF(last_error = '', ?, last_error),
    updated_at = ?
WHERE batch_id = ?
  AND state IN ('Pending', 'Running')
