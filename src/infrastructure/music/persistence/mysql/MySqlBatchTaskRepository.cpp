#include "crawler/infrastructure/music/persistence/mysql/MySqlBatchTaskRepository.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlWriteRunner.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/mysql/error_code.hpp>
#include <boost/mysql/results.hpp>

#include <algorithm>
#include <ctime>
#include <exception>
#include <future>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

namespace {

constexpr const char *kInsertBatchSql = R"SQL(
INSERT INTO crawl_batches (
    batch_id, source, task_type, state, request_params, settings_snapshot, limit_count, target_count,
    processed_count, success_count, failure_count, cancel_requested, last_error,
    created_at, started_at, finished_at, updated_at, archived_at, archived_reason, parent_batch_id
) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
ON DUPLICATE KEY UPDATE
    state = VALUES(state),
    request_params = VALUES(request_params),
    settings_snapshot = VALUES(settings_snapshot),
    limit_count = VALUES(limit_count),
    target_count = VALUES(target_count),
    archived_at = VALUES(archived_at),
    archived_reason = VALUES(archived_reason),
    parent_batch_id = VALUES(parent_batch_id),
    updated_at = VALUES(updated_at)
)SQL";

constexpr const char *kInsertItemSql = R"SQL(
INSERT INTO crawl_batch_items (
    batch_id, item_index, item_key, item_type, item_label, source, source_singer_id, source_album_id,
    source_song_id, state, retry_count, written_count, error_category, result_code, started_at, finished_at, last_error,
    payload, created_at, updated_at
) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
ON DUPLICATE KEY UPDATE
    item_key = VALUES(item_key),
    item_type = VALUES(item_type),
    item_label = VALUES(item_label),
    source_singer_id = VALUES(source_singer_id),
    source_album_id = VALUES(source_album_id),
    source_song_id = VALUES(source_song_id),
    state = VALUES(state),
    retry_count = VALUES(retry_count),
    written_count = VALUES(written_count),
    error_category = VALUES(error_category),
    result_code = VALUES(result_code),
    started_at = VALUES(started_at),
    finished_at = VALUES(finished_at),
    last_error = VALUES(last_error),
    payload = VALUES(payload),
    updated_at = VALUES(updated_at)
)SQL";

constexpr const char *kUpdateBatchSql = R"SQL(
UPDATE crawl_batches
SET state = IF(? = '', state, ?),
    target_count = IF(? = 0, target_count, ?),
    processed_count = ?,
    success_count = ?,
    failure_count = ?,
    cancel_requested = ?,
    last_error = IF(? = '', last_error, ?),
    finished_at = IF(? = 0, finished_at, ?),
    updated_at = ?
WHERE batch_id = ?
)SQL";

constexpr const char *kUpdateItemSql = R"SQL(
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
)SQL";

constexpr const char *kInsertEventSql = R"SQL(
INSERT INTO crawl_batch_events (batch_id, item_index, event_type, message, payload, created_at)
VALUES (?, ?, ?, ?, ?, ?)
)SQL";

constexpr const char *kListBatchesSql = R"SQL(
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
)SQL";

constexpr const char *kCountBatchesSql = R"SQL(
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
)SQL";

constexpr const char *kFindBatchSql = R"SQL(
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
)SQL";

constexpr const char *kListItemsSql = R"SQL(
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
)SQL";

constexpr const char *kCountItemsSql = R"SQL(
SELECT COUNT(*)
FROM crawl_batch_items
WHERE batch_id = ?
  AND (? = '' OR state = ?)
  AND (? = '' OR error_category = ?)
  AND (? = '' OR result_code = ?)
)SQL";

constexpr const char *kListEventsSql = R"SQL(
SELECT batch_id, item_index, event_type, COALESCE(message, ''), COALESCE(CAST(payload AS CHAR), '{}'), created_at
FROM crawl_batch_events
WHERE batch_id = ?
ORDER BY id DESC
LIMIT ? OFFSET ?
)SQL";

constexpr const char *kCountEventsSql = R"SQL(
SELECT COUNT(*)
FROM crawl_batch_events
WHERE batch_id = ?
)SQL";

constexpr const char *kSelectStaleBatchesSql = R"SQL(
SELECT batch_id
FROM crawl_batches
WHERE source = ?
  AND state = 'Running'
  AND updated_at <= UNIX_TIMESTAMP() - ?
ORDER BY updated_at ASC, id ASC
)SQL";

constexpr const char *kRecoverStaleItemsSql = R"SQL(
UPDATE crawl_batch_items
SET state = 'Failed',
    error_category = 'timeout',
    result_code = 'timeout',
    finished_at = IF(finished_at = 0, ?, finished_at),
    last_error = IF(last_error = '', ?, last_error),
    updated_at = ?
WHERE batch_id = ?
  AND state IN ('Pending', 'Running')
)SQL";

constexpr const char *kRecoverStaleBatchSql = R"SQL(
UPDATE crawl_batches
SET state = 'Failed',
    processed_count = target_count,
    failure_count = GREATEST(failure_count, GREATEST(target_count - success_count, 0)),
    last_error = IF(last_error = '', ?, last_error),
    finished_at = IF(finished_at = 0, ?, finished_at),
    updated_at = ?
WHERE batch_id = ?
  AND state = 'Running'
)SQL";

constexpr const char *kArchiveBatchSql = R"SQL(
UPDATE crawl_batches
SET archived_at = UNIX_TIMESTAMP(),
    archived_reason = NULLIF(CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, ''),
    updated_at = UNIX_TIMESTAMP()
WHERE batch_id = ?
)SQL";

constexpr const char *kStateDistributionSql = R"SQL(
SELECT state, COUNT(*)
FROM crawl_batches
WHERE source = ?
  AND archived_at = 0
GROUP BY state
ORDER BY state ASC
)SQL";

constexpr const char *kFailureTaskTypesSql = R"SQL(
SELECT task_type, COUNT(*)
FROM crawl_batches
WHERE source = ?
  AND archived_at = 0
  AND state = 'Failed'
GROUP BY task_type
ORDER BY COUNT(*) DESC, task_type ASC
LIMIT 10
)SQL";

constexpr const char *kRunningCountSql = R"SQL(
SELECT COUNT(*)
FROM crawl_batches
WHERE source = ?
  AND archived_at = 0
  AND state = 'Running'
)SQL";

constexpr const char *kOldestStaleRunningSql = R"SQL(
SELECT COALESCE(MIN(updated_at), 0)
FROM crawl_batches
WHERE source = ?
  AND archived_at = 0
  AND state = 'Running'
  AND updated_at <= UNIX_TIMESTAMP() - ?
)SQL";

constexpr const char *kLatestBatchByStateSql = R"SQL(
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
WHERE b.source = ?
  AND b.archived_at = 0
  AND b.state = ?
ORDER BY b.finished_at DESC, b.updated_at DESC, b.id DESC
LIMIT 1
)SQL";

template <typename T>
T waitForResult(boost::asio::any_io_executor executor, boost::asio::awaitable<T> task, std::string_view context, T fallback)
{
    auto future = boost::asio::co_spawn(std::move(executor), std::move(task), boost::asio::use_future);
    try {
        return future.get();
    } catch (const std::exception &e) {
        std::cerr << "[MySqlBatchTaskRepository] " << context << " failed: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[MySqlBatchTaskRepository] " << context << " failed with unknown error" << std::endl;
    }
    return fallback;
}

std::uint64_t countFromResult(const boost::mysql::results &result)
{
    if (result.rows().empty()) {
        return 0;
    }
    return MySqlValueReader::uint64At(result.rows().front(), 0);
}

} // namespace

MySqlBatchTaskRepository::MySqlBatchTaskRepository(std::shared_ptr<MySqlConnectionPool> pool) : pool_(std::move(pool))
{
    if (!pool_) {
        throw std::invalid_argument("[MySqlBatchTaskRepository] pool must not be null");
    }
}

bool MySqlBatchTaskRepository::createBatch(const application::model::CrawlBatchRecord &batch, const std::vector<application::model::CrawlBatchItemRecord> &items)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coCreateBatch(batch, items), "MySqlBatchTaskRepository", "createBatch");
}

bool MySqlBatchTaskRepository::updateBatchProgress(const application::model::CrawlBatchRecord &batch)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coUpdateBatchProgress(batch), "MySqlBatchTaskRepository", "updateBatchProgress");
}

bool MySqlBatchTaskRepository::updateItem(const application::model::CrawlBatchItemRecord &item)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coUpdateItem(item), "MySqlBatchTaskRepository", "updateItem");
}

bool MySqlBatchTaskRepository::upsertItem(const application::model::CrawlBatchItemRecord &item)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coUpsertItem(item), "MySqlBatchTaskRepository", "upsertItem");
}

bool MySqlBatchTaskRepository::appendEvent(const application::model::CrawlBatchEventRecord &event)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coAppendEvent(event), "MySqlBatchTaskRepository", "appendEvent");
}

application::model::CrawlBatchPage MySqlBatchTaskRepository::listBatches(const application::model::CrawlBatchQuery &query)
{
    return waitForResult(pool_->executor(), coListBatches(query), "listBatches", application::model::CrawlBatchPage {});
}

std::optional<application::model::CrawlBatchRecord> MySqlBatchTaskRepository::findBatch(std::string batch_id)
{
    return waitForResult<std::optional<application::model::CrawlBatchRecord>>(pool_->executor(), coFindBatch(std::move(batch_id)), "findBatch", std::nullopt);
}

application::model::CrawlBatchItemPage MySqlBatchTaskRepository::listItems(std::string batch_id, const application::model::CrawlBatchItemQuery &query)
{
    return waitForResult(pool_->executor(), coListItems(std::move(batch_id), query), "listItems", application::model::CrawlBatchItemPage {});
}

application::model::CrawlBatchEventPage MySqlBatchTaskRepository::listEvents(std::string batch_id, const application::model::CrawlBatchEventQuery &query)
{
    return waitForResult(pool_->executor(), coListEvents(std::move(batch_id), query), "listEvents", application::model::CrawlBatchEventPage {});
}

application::model::CrawlBatchSummary MySqlBatchTaskRepository::summary(std::string source, int stale_after_seconds)
{
    return waitForResult(pool_->executor(), coSummary(std::move(source), stale_after_seconds), "summary", application::model::CrawlBatchSummary {});
}

bool MySqlBatchTaskRepository::archiveBatch(std::string batch_id, std::string reason)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coArchiveBatch(std::move(batch_id), std::move(reason)), "MySqlBatchTaskRepository", "archiveBatch");
}

int MySqlBatchTaskRepository::recoverStaleRunningBatches(std::string source, int stale_after_seconds, std::string error)
{
    return waitForResult(pool_->executor(), coRecoverStaleRunningBatches(std::move(source), stale_after_seconds, std::move(error)), "recoverStaleRunningBatches", 0);
}

boost::asio::awaitable<bool> MySqlBatchTaskRepository::coCreateBatch(application::model::CrawlBatchRecord batch, std::vector<application::model::CrawlBatchItemRecord> items)
{
    auto connection = co_await pool_->coAcquire();
    boost::mysql::results result;
    co_await connection->async_execute("START TRANSACTION", result, boost::asio::use_awaitable);
    bool needs_rollback = false;
    try {
        auto batch_statement = co_await connection->async_prepare_statement(kInsertBatchSql, boost::asio::use_awaitable);
        co_await connection->async_execute(
            batch_statement.bind(batch.batch_id, batch.source, batch.task_type, batch.state, batch.request_params, batch.settings_snapshot, batch.limit_count, batch.target_count, batch.processed_count, batch.success_count, batch.failure_count, batch.cancel_requested ? 1 : 0, batch.last_error, batch.created_at, batch.started_at, batch.finished_at, batch.updated_at, batch.archived_at, batch.archived_reason, batch.parent_batch_id),
            result,
            boost::asio::use_awaitable
        );

        auto item_statement = co_await connection->async_prepare_statement(kInsertItemSql, boost::asio::use_awaitable);
        for (const auto &item : items) {
            co_await connection->async_execute(
                item_statement.bind(item.batch_id, item.item_index, item.item_key, item.item_type, item.item_label, item.source, item.source_singer_id, item.source_album_id, item.source_song_id, item.state, item.retry_count, item.written_count, item.error_category, item.result_code, item.started_at, item.finished_at, item.last_error, item.payload.empty() ? "{}" : item.payload, item.created_at, item.updated_at),
                result,
                boost::asio::use_awaitable
            );
        }

        co_await connection->async_execute("COMMIT", result, boost::asio::use_awaitable);
        co_return true;
    } catch (const std::exception &e) {
        std::cerr << "[MySqlBatchTaskRepository] coCreateBatch failed: " << e.what() << std::endl;
        needs_rollback = true;
    }

    if (needs_rollback) {
        boost::mysql::error_code ignored_error;
        co_await connection->async_execute("ROLLBACK", result, boost::asio::redirect_error(boost::asio::use_awaitable, ignored_error));
    }
    co_return false;
}

boost::asio::awaitable<bool> MySqlBatchTaskRepository::coUpdateBatchProgress(application::model::CrawlBatchRecord batch)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(kUpdateBatchSql, boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(batch.state, batch.state, batch.target_count, batch.target_count, batch.processed_count, batch.success_count, batch.failure_count, batch.cancel_requested ? 1 : 0, batch.last_error, batch.last_error, batch.finished_at, batch.finished_at, batch.updated_at, batch.batch_id), result, boost::asio::use_awaitable);
    co_return result.affected_rows() > 0;
}

boost::asio::awaitable<bool> MySqlBatchTaskRepository::coUpdateItem(application::model::CrawlBatchItemRecord item)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(kUpdateItemSql, boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(item.state, item.retry_count, item.written_count, item.error_category, item.error_category, item.result_code, item.result_code, item.started_at, item.started_at, item.finished_at, item.finished_at, item.last_error, item.last_error, item.payload, item.payload.empty() ? "{}" : item.payload, item.updated_at, item.batch_id, item.item_index), result, boost::asio::use_awaitable);
    co_return result.affected_rows() > 0;
}

boost::asio::awaitable<bool> MySqlBatchTaskRepository::coUpsertItem(application::model::CrawlBatchItemRecord item)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(kInsertItemSql, boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(item.batch_id, item.item_index, item.item_key, item.item_type, item.item_label, item.source, item.source_singer_id, item.source_album_id, item.source_song_id, item.state, item.retry_count, item.written_count, item.error_category, item.result_code, item.started_at, item.finished_at, item.last_error, item.payload.empty() ? "{}" : item.payload, item.created_at, item.updated_at), result, boost::asio::use_awaitable);
    co_return result.affected_rows() > 0;
}

boost::asio::awaitable<bool> MySqlBatchTaskRepository::coAppendEvent(application::model::CrawlBatchEventRecord event)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(kInsertEventSql, boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(event.batch_id, event.item_index, event.event_type, event.message, event.payload.empty() ? "{}" : event.payload, event.created_at), result, boost::asio::use_awaitable);
    co_return result.affected_rows() > 0;
}

boost::asio::awaitable<application::model::CrawlBatchPage> MySqlBatchTaskRepository::coListBatches(application::model::CrawlBatchQuery query)
{
    application::model::CrawlBatchPage page;
    page.page = query.page;
    page.page_size = query.page_size;
    const auto offset = static_cast<std::uint64_t>((query.page - 1) * query.page_size);
    auto connection = co_await pool_->coAcquire();
    boost::mysql::results result;
    auto count_statement = co_await connection->async_prepare_statement(kCountBatchesSql, boost::asio::use_awaitable);
    co_await connection->async_execute(count_statement.bind(query.state, query.task_type, query.keyword, query.display_name, query.target_summary, query.target_keywords, query.last_error, query.error_category, query.result_code, query.include_archived ? 1 : 0), result, boost::asio::use_awaitable);
    page.total = countFromResult(result);

    auto list_statement = co_await connection->async_prepare_statement(kListBatchesSql, boost::asio::use_awaitable);
    co_await connection->async_execute(list_statement.bind(query.state, query.task_type, query.keyword, query.display_name, query.target_summary, query.target_keywords, query.last_error, query.error_category, query.result_code, query.include_archived ? 1 : 0, static_cast<std::uint64_t>(query.page_size), offset), result, boost::asio::use_awaitable);
    for (auto row : result.rows()) {
        page.items.push_back(mapBatch(row));
    }
    co_return page;
}

boost::asio::awaitable<std::optional<application::model::CrawlBatchRecord>> MySqlBatchTaskRepository::coFindBatch(std::string batch_id)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(kFindBatchSql, boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(std::move(batch_id)), result, boost::asio::use_awaitable);
    if (result.rows().empty()) {
        co_return std::nullopt;
    }
    co_return mapBatch(result.rows().front());
}

boost::asio::awaitable<application::model::CrawlBatchItemPage> MySqlBatchTaskRepository::coListItems(std::string batch_id, application::model::CrawlBatchItemQuery query)
{
    application::model::CrawlBatchItemPage page;
    page.page = query.page;
    page.page_size = query.page_size;
    const auto offset = static_cast<std::uint64_t>((query.page - 1) * query.page_size);
    auto connection = co_await pool_->coAcquire();
    boost::mysql::results result;
    auto count_statement = co_await connection->async_prepare_statement(kCountItemsSql, boost::asio::use_awaitable);
    co_await connection->async_execute(count_statement.bind(batch_id, query.state, query.state, query.error_category, query.error_category, query.result_code, query.result_code), result, boost::asio::use_awaitable);
    page.total = countFromResult(result);

    auto list_statement = co_await connection->async_prepare_statement(kListItemsSql, boost::asio::use_awaitable);
    co_await connection->async_execute(list_statement.bind(std::move(batch_id), query.state, query.state, query.error_category, query.error_category, query.result_code, query.result_code, static_cast<std::uint64_t>(query.page_size), offset), result, boost::asio::use_awaitable);
    for (auto row : result.rows()) {
        page.items.push_back(mapItem(row));
    }
    co_return page;
}

boost::asio::awaitable<application::model::CrawlBatchEventPage> MySqlBatchTaskRepository::coListEvents(std::string batch_id, application::model::CrawlBatchEventQuery query)
{
    application::model::CrawlBatchEventPage page;
    page.page = query.page;
    page.page_size = query.page_size;
    const auto offset = static_cast<std::uint64_t>((query.page - 1) * query.page_size);
    auto connection = co_await pool_->coAcquire();
    boost::mysql::results result;
    auto count_statement = co_await connection->async_prepare_statement(kCountEventsSql, boost::asio::use_awaitable);
    co_await connection->async_execute(count_statement.bind(batch_id), result, boost::asio::use_awaitable);
    page.total = countFromResult(result);

    auto list_statement = co_await connection->async_prepare_statement(kListEventsSql, boost::asio::use_awaitable);
    co_await connection->async_execute(list_statement.bind(std::move(batch_id), static_cast<std::uint64_t>(query.page_size), offset), result, boost::asio::use_awaitable);
    for (auto row : result.rows()) {
        page.items.push_back(mapEvent(row));
    }
    co_return page;
}

boost::asio::awaitable<application::model::CrawlBatchSummary> MySqlBatchTaskRepository::coSummary(std::string source, int stale_after_seconds)
{
    application::model::CrawlBatchSummary summary;
    summary.generated_at = static_cast<std::uint64_t>(std::time(nullptr));
    if (source.empty()) {
        source = "migu";
    }
    stale_after_seconds = (std::max)(1, stale_after_seconds);
    auto connection = co_await pool_->coAcquire();
    boost::mysql::results result;

    auto state_statement = co_await connection->async_prepare_statement(kStateDistributionSql, boost::asio::use_awaitable);
    co_await connection->async_execute(state_statement.bind(source), result, boost::asio::use_awaitable);
    for (boost::mysql::row_view row : result.rows()) {
        summary.state_distribution.emplace_back(MySqlValueReader::stringAt(row, 0), MySqlValueReader::uint64At(row, 1));
    }

    auto failure_statement = co_await connection->async_prepare_statement(kFailureTaskTypesSql, boost::asio::use_awaitable);
    co_await connection->async_execute(failure_statement.bind(source), result, boost::asio::use_awaitable);
    for (boost::mysql::row_view row : result.rows()) {
        summary.failure_task_types.emplace_back(MySqlValueReader::stringAt(row, 0), MySqlValueReader::uint64At(row, 1));
    }

    auto running_statement = co_await connection->async_prepare_statement(kRunningCountSql, boost::asio::use_awaitable);
    co_await connection->async_execute(running_statement.bind(source), result, boost::asio::use_awaitable);
    summary.running_count = countFromResult(result);

    auto stale_statement = co_await connection->async_prepare_statement(kOldestStaleRunningSql, boost::asio::use_awaitable);
    co_await connection->async_execute(stale_statement.bind(source, stale_after_seconds), result, boost::asio::use_awaitable);
    summary.oldest_stale_running_at = countFromResult(result);

    auto latest_statement = co_await connection->async_prepare_statement(kLatestBatchByStateSql, boost::asio::use_awaitable);
    co_await connection->async_execute(latest_statement.bind(source, "Completed"), result, boost::asio::use_awaitable);
    if (!result.rows().empty()) {
        summary.latest_success = mapBatch(result.rows().front());
    }
    co_await connection->async_execute(latest_statement.bind(source, "Failed"), result, boost::asio::use_awaitable);
    if (!result.rows().empty()) {
        summary.latest_failed = mapBatch(result.rows().front());
    }

    co_return summary;
}

boost::asio::awaitable<bool> MySqlBatchTaskRepository::coArchiveBatch(std::string batch_id, std::string reason)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(kArchiveBatchSql, boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(std::move(reason), std::move(batch_id)), result, boost::asio::use_awaitable);
    co_return result.affected_rows() > 0;
}

boost::asio::awaitable<int> MySqlBatchTaskRepository::coRecoverStaleRunningBatches(std::string source, int stale_after_seconds, std::string error)
{
    if (stale_after_seconds <= 0) {
        stale_after_seconds = 1;
    }
    auto connection = co_await pool_->coAcquire();
    boost::mysql::results result;
    auto select_statement = co_await connection->async_prepare_statement(kSelectStaleBatchesSql, boost::asio::use_awaitable);
    co_await connection->async_execute(select_statement.bind(source, stale_after_seconds), result, boost::asio::use_awaitable);

    std::vector<std::string> batch_ids;
    batch_ids.reserve(result.rows().size());
    for (boost::mysql::row_view row : result.rows()) {
        batch_ids.push_back(MySqlValueReader::stringAt(row, 0));
    }
    if (batch_ids.empty()) {
        co_return 0;
    }

    const auto now = static_cast<std::uint64_t>(std::time(nullptr));
    auto item_statement = co_await connection->async_prepare_statement(kRecoverStaleItemsSql, boost::asio::use_awaitable);
    auto batch_statement = co_await connection->async_prepare_statement(kRecoverStaleBatchSql, boost::asio::use_awaitable);
    auto event_statement = co_await connection->async_prepare_statement(kInsertEventSql, boost::asio::use_awaitable);
    int recovered = 0;
    for (const auto &batch_id : batch_ids) {
        boost::mysql::results write_result;
        co_await connection->async_execute("START TRANSACTION", write_result, boost::asio::use_awaitable);
        bool needs_rollback = false;
        try {
            co_await connection->async_execute(item_statement.bind(now, error, now, batch_id), write_result, boost::asio::use_awaitable);
            const auto recovered_items = write_result.affected_rows();
            co_await connection->async_execute(batch_statement.bind(error, now, now, batch_id), write_result, boost::asio::use_awaitable);
            if (write_result.affected_rows() > 0) {
                const std::string payload = "{\"stage\":\"batch_recovered\",\"recovered_items\":" + std::to_string(recovered_items) + "}";
                co_await connection->async_execute(event_statement.bind(batch_id, std::optional<int> {}, "timeoutRecovered", error, payload, now), write_result, boost::asio::use_awaitable);
                ++recovered;
            }
            co_await connection->async_execute("COMMIT", write_result, boost::asio::use_awaitable);
        } catch (const std::exception &e) {
            std::cerr << "[MySqlBatchTaskRepository] coRecoverStaleRunningBatches failed for batch " << batch_id << ": " << e.what() << std::endl;
            needs_rollback = true;
        }
        if (needs_rollback) {
            boost::mysql::error_code ignored_error;
            co_await connection->async_execute("ROLLBACK", write_result, boost::asio::redirect_error(boost::asio::use_awaitable, ignored_error));
        }
    }
    co_return recovered;
}

application::model::CrawlBatchRecord MySqlBatchTaskRepository::mapBatch(boost::mysql::row_view row)
{
    application::model::CrawlBatchRecord batch;
    batch.batch_id = MySqlValueReader::stringAt(row, 0);
    batch.source = MySqlValueReader::stringAt(row, 1);
    batch.task_type = MySqlValueReader::stringAt(row, 2);
    batch.state = MySqlValueReader::stringAt(row, 3);
    batch.request_params = MySqlValueReader::stringAt(row, 4);
    batch.settings_snapshot = MySqlValueReader::stringAt(row, 5);
    batch.limit_count = MySqlValueReader::intAt(row, 6);
    batch.target_count = MySqlValueReader::intAt(row, 7);
    batch.processed_count = MySqlValueReader::intAt(row, 8);
    batch.success_count = MySqlValueReader::intAt(row, 9);
    batch.failure_count = MySqlValueReader::intAt(row, 10);
    batch.cancel_requested = MySqlValueReader::intAt(row, 11) != 0;
    batch.last_error = MySqlValueReader::stringAt(row, 12);
    batch.created_at = MySqlValueReader::uint64At(row, 13);
    batch.started_at = MySqlValueReader::uint64At(row, 14);
    batch.finished_at = MySqlValueReader::uint64At(row, 15);
    batch.updated_at = MySqlValueReader::uint64At(row, 16);
    batch.archived_at = MySqlValueReader::uint64At(row, 17);
    batch.archived_reason = MySqlValueReader::stringAt(row, 18);
    batch.parent_batch_id = MySqlValueReader::stringAt(row, 19);
    batch.display_name = MySqlValueReader::stringAt(row, 20);
    batch.target_summary = MySqlValueReader::stringAt(row, 21);
    batch.target_keywords = MySqlValueReader::stringAt(row, 22);
    batch.error_category = MySqlValueReader::stringAt(row, 23);
    batch.result_code = MySqlValueReader::stringAt(row, 24);
    return batch;
}

application::model::CrawlBatchItemRecord MySqlBatchTaskRepository::mapItem(boost::mysql::row_view row)
{
    application::model::CrawlBatchItemRecord item;
    item.batch_id = MySqlValueReader::stringAt(row, 0);
    item.item_index = MySqlValueReader::intAt(row, 1);
    item.item_key = MySqlValueReader::stringAt(row, 2);
    item.item_type = MySqlValueReader::stringAt(row, 3);
    item.item_label = MySqlValueReader::stringAt(row, 4);
    item.source = MySqlValueReader::stringAt(row, 5);
    item.source_singer_id = MySqlValueReader::stringAt(row, 6);
    item.source_album_id = MySqlValueReader::stringAt(row, 7);
    item.source_song_id = MySqlValueReader::stringAt(row, 8);
    item.state = MySqlValueReader::stringAt(row, 9);
    item.retry_count = MySqlValueReader::intAt(row, 10);
    item.written_count = MySqlValueReader::intAt(row, 11);
    item.error_category = MySqlValueReader::stringAt(row, 12);
    item.result_code = MySqlValueReader::stringAt(row, 13);
    item.started_at = MySqlValueReader::uint64At(row, 14);
    item.finished_at = MySqlValueReader::uint64At(row, 15);
    item.last_error = MySqlValueReader::stringAt(row, 16);
    item.payload = MySqlValueReader::stringAt(row, 17);
    item.created_at = MySqlValueReader::uint64At(row, 18);
    item.updated_at = MySqlValueReader::uint64At(row, 19);
    return item;
}

application::model::CrawlBatchEventRecord MySqlBatchTaskRepository::mapEvent(boost::mysql::row_view row)
{
    application::model::CrawlBatchEventRecord event;
    event.batch_id = MySqlValueReader::stringAt(row, 0);
    if (row.size() > 1 && !row[1].is_null()) {
        event.item_index = MySqlValueReader::intAt(row, 1);
    }
    event.event_type = MySqlValueReader::stringAt(row, 2);
    event.message = MySqlValueReader::stringAt(row, 3);
    event.payload = MySqlValueReader::stringAt(row, 4);
    event.created_at = MySqlValueReader::uint64At(row, 5);
    return event;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
