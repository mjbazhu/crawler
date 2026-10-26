SELECT b.batch_id, b.source, b.task_type, b.state, COALESCE(CAST(b.request_params AS CHAR), '{}'), COALESCE(CAST(b.settings_snapshot AS CHAR), '{}'),
       b.limit_count, b.target_count, b.processed_count, b.success_count, b.failure_count, b.cancel_requested, COALESCE(b.last_error, ''),
       b.created_at, b.started_at, b.finished_at, b.updated_at, b.archived_at, COALESCE(b.archived_reason, ''), COALESCE(b.parent_batch_id, ''),
       COALESCE(NULLIF(item_meta.display_name, ''), b.batch_id), COALESCE(item_meta.target_summary, ''), COALESCE(item_meta.target_keywords, ''),
       COALESCE(item_meta.error_category, ''), COALESCE(item_meta.result_code, '')
FROM crawl_batches b
LEFT JOIN (
    SELECT batch_id,
           COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(COALESCE(NULLIF(item_label, ''), NULLIF(source_singer_id, ''), NULLIF(source_album_id, ''), NULLIF(source_song_id, ''), item_key) ORDER BY item_index SEPARATOR ' / '), ' / ', 1), '') AS display_name,
           COALESCE(GROUP_CONCAT(CONCAT_WS(' ', COALESCE(NULLIF(item_label, ''), item_key), NULLIF(source_singer_id, ''), NULLIF(source_album_id, ''), NULLIF(source_song_id, ''), CASE WHEN COALESCE(last_error, '') = '' THEN NULL ELSE CONCAT('error:', last_error) END) ORDER BY item_index SEPARATOR ' | '), '') AS target_summary,
           COALESCE(GROUP_CONCAT(CONCAT_WS(' ', item_key, item_type, NULLIF(item_label, ''), NULLIF(source_singer_id, ''), NULLIF(source_album_id, ''), NULLIF(source_song_id, ''), NULLIF(last_error, ''), NULLIF(error_category, ''), NULLIF(result_code, '')) ORDER BY item_index SEPARATOR ' '), '') AS target_keywords,
           COALESCE(GROUP_CONCAT(DISTINCT NULLIF(error_category, '') ORDER BY NULLIF(error_category, '') SEPARATOR ','), '') AS error_category,
           COALESCE(GROUP_CONCAT(DISTINCT NULLIF(result_code, '') ORDER BY NULLIF(result_code, '') SEPARATOR ','), '') AS result_code
    FROM crawl_batch_items
    GROUP BY batch_id
) item_meta ON item_meta.batch_id = b.batch_id
WHERE b.batch_id = ?
LIMIT 1
