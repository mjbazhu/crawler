SELECT batch_id, item_index, event_type, COALESCE(message, ''), COALESCE(CAST(payload AS CHAR), '{}'), created_at
FROM crawl_batch_events
WHERE batch_id = ?
ORDER BY id DESC
LIMIT ? OFFSET ?
