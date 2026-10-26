SELECT COUNT(*)
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
