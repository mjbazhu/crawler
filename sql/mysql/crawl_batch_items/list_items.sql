SELECT batch_id, item_index, item_key, item_type, COALESCE(item_label, ''), source, COALESCE(source_singer_id, ''),
       COALESCE(source_album_id, ''), COALESCE(source_song_id, ''), state, retry_count, written_count,
       COALESCE(error_category, ''), COALESCE(result_code, ''), started_at, finished_at, COALESCE(last_error, ''), COALESCE(CAST(payload AS CHAR), '{}'), created_at, updated_at
FROM crawl_batch_items
WHERE batch_id = ?
  AND (? = '' OR state = ?)
  AND (? = '' OR error_category = ?)
  AND (? = '' OR result_code = ?)
ORDER BY item_index ASC
LIMIT ? OFFSET ?
