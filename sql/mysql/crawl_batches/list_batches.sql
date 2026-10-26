SELECT b.batch_id, b.source, b.task_type, b.state, COALESCE(CAST(b.request_params AS CHAR), '{}'), COALESCE(CAST(b.settings_snapshot AS CHAR), '{}'),
       b.limit_count, b.target_count, b.processed_count, b.success_count, b.failure_count, b.cancel_requested, COALESCE(b.last_error, ''),
       b.created_at, b.started_at, b.finished_at, b.updated_at, b.archived_at, COALESCE(b.archived_reason, ''), COALESCE(b.parent_batch_id, ''),
       COALESCE(NULLIF(item_meta.display_name, ''), b.batch_id), COALESCE(item_meta.target_summary, ''), COALESCE(item_meta.target_keywords, ''),
       COALESCE(item_meta.error_category, ''), COALESCE(item_meta.result_code, '')
FROM crawl_batches b
CROSS JOIN (
    SELECT
        CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci AS state_filter,
        CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci AS type_filter,
        CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci AS q_filter,
        CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci AS display_name_filter,
        CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci AS target_summary_filter,
        CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci AS target_keywords_filter,
        CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci AS last_error_filter,
        CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci AS error_category_filter,
        CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci AS result_code_filter,
        CAST(? AS UNSIGNED) AS include_archived_filter
) filters
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
WHERE (filters.state_filter = '' OR b.state = filters.state_filter)
  AND (filters.type_filter = '' OR b.task_type = filters.type_filter)
  AND (
      filters.q_filter = ''
      OR b.batch_id LIKE CONCAT('%', filters.q_filter, '%')
      OR b.task_type LIKE CONCAT('%', filters.q_filter, '%')
      OR COALESCE(b.last_error, '') LIKE CONCAT('%', filters.q_filter, '%')
      OR COALESCE(CAST(b.request_params AS CHAR), '') LIKE CONCAT('%', filters.q_filter, '%')
      OR EXISTS (
          SELECT 1
          FROM crawl_batch_items q_item
          WHERE q_item.batch_id = b.batch_id
            AND (
                COALESCE(q_item.item_label, '') LIKE CONCAT('%', filters.q_filter, '%')
                OR q_item.item_key LIKE CONCAT('%', filters.q_filter, '%')
                OR q_item.item_type LIKE CONCAT('%', filters.q_filter, '%')
                OR COALESCE(q_item.source_singer_id, '') LIKE CONCAT('%', filters.q_filter, '%')
                OR COALESCE(q_item.source_album_id, '') LIKE CONCAT('%', filters.q_filter, '%')
                OR COALESCE(q_item.source_song_id, '') LIKE CONCAT('%', filters.q_filter, '%')
                OR COALESCE(q_item.last_error, '') LIKE CONCAT('%', filters.q_filter, '%')
                OR COALESCE(q_item.error_category, '') LIKE CONCAT('%', filters.q_filter, '%')
                OR COALESCE(q_item.result_code, '') LIKE CONCAT('%', filters.q_filter, '%')
            )
      )
  )
  AND (
      filters.display_name_filter = ''
      OR EXISTS (
          SELECT 1
          FROM crawl_batch_items display_item
          WHERE display_item.batch_id = b.batch_id
            AND (
                COALESCE(display_item.item_label, '') LIKE CONCAT('%', filters.display_name_filter, '%')
                OR display_item.item_key LIKE CONCAT('%', filters.display_name_filter, '%')
                OR COALESCE(display_item.source_singer_id, '') LIKE CONCAT('%', filters.display_name_filter, '%')
                OR COALESCE(display_item.source_album_id, '') LIKE CONCAT('%', filters.display_name_filter, '%')
                OR COALESCE(display_item.source_song_id, '') LIKE CONCAT('%', filters.display_name_filter, '%')
            )
      )
  )
  AND (
      filters.target_summary_filter = ''
      OR EXISTS (
          SELECT 1
          FROM crawl_batch_items summary_item
          WHERE summary_item.batch_id = b.batch_id
            AND (
                COALESCE(summary_item.item_label, '') LIKE CONCAT('%', filters.target_summary_filter, '%')
                OR summary_item.item_key LIKE CONCAT('%', filters.target_summary_filter, '%')
                OR COALESCE(summary_item.source_singer_id, '') LIKE CONCAT('%', filters.target_summary_filter, '%')
                OR COALESCE(summary_item.source_album_id, '') LIKE CONCAT('%', filters.target_summary_filter, '%')
                OR COALESCE(summary_item.source_song_id, '') LIKE CONCAT('%', filters.target_summary_filter, '%')
                OR COALESCE(summary_item.last_error, '') LIKE CONCAT('%', filters.target_summary_filter, '%')
                OR COALESCE(summary_item.error_category, '') LIKE CONCAT('%', filters.target_summary_filter, '%')
                OR COALESCE(summary_item.result_code, '') LIKE CONCAT('%', filters.target_summary_filter, '%')
            )
      )
  )
  AND (
      filters.target_keywords_filter = ''
      OR EXISTS (
          SELECT 1
          FROM crawl_batch_items keyword_item
          WHERE keyword_item.batch_id = b.batch_id
            AND (
                COALESCE(keyword_item.item_label, '') LIKE CONCAT('%', filters.target_keywords_filter, '%')
                OR keyword_item.item_key LIKE CONCAT('%', filters.target_keywords_filter, '%')
                OR keyword_item.item_type LIKE CONCAT('%', filters.target_keywords_filter, '%')
                OR COALESCE(keyword_item.source_singer_id, '') LIKE CONCAT('%', filters.target_keywords_filter, '%')
                OR COALESCE(keyword_item.source_album_id, '') LIKE CONCAT('%', filters.target_keywords_filter, '%')
                OR COALESCE(keyword_item.source_song_id, '') LIKE CONCAT('%', filters.target_keywords_filter, '%')
                OR COALESCE(keyword_item.last_error, '') LIKE CONCAT('%', filters.target_keywords_filter, '%')
                OR COALESCE(keyword_item.error_category, '') LIKE CONCAT('%', filters.target_keywords_filter, '%')
                OR COALESCE(keyword_item.result_code, '') LIKE CONCAT('%', filters.target_keywords_filter, '%')
            )
      )
  )
  AND (
      filters.last_error_filter = ''
      OR COALESCE(b.last_error, '') LIKE CONCAT('%', filters.last_error_filter, '%')
      OR EXISTS (
          SELECT 1
          FROM crawl_batch_items error_item
          WHERE error_item.batch_id = b.batch_id
            AND COALESCE(error_item.last_error, '') LIKE CONCAT('%', filters.last_error_filter, '%')
      )
  )
  AND (
      filters.error_category_filter = ''
      OR EXISTS (
          SELECT 1
          FROM crawl_batch_items category_item
          WHERE category_item.batch_id = b.batch_id
            AND COALESCE(category_item.error_category, '') = filters.error_category_filter
      )
  )
  AND (
      filters.result_code_filter = ''
      OR EXISTS (
          SELECT 1
          FROM crawl_batch_items result_item
          WHERE result_item.batch_id = b.batch_id
            AND COALESCE(result_item.result_code, '') = filters.result_code_filter
      )
  )
  AND (filters.include_archived_filter = 1 OR b.archived_at = 0)
ORDER BY b.created_at DESC, b.id DESC
LIMIT ? OFFSET ?
