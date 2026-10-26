UPDATE crawl_batch_items
SET state = ?,
    retry_count = ?,
    written_count = ?,
    error_category = IF(? = '', error_category, ?),
    result_code = IF(? = '', result_code, ?),
    started_at = IF(? = 0, started_at, ?),
    finished_at = IF(? = 0, finished_at, ?),
    last_error = IF(? = '', last_error, ?),
    payload = IF(? = '', payload, ?),
    updated_at = ?
WHERE batch_id = ? AND item_index = ?
