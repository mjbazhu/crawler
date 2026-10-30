UPDATE song_resources
SET status = 0,
    retry_count = 0,
    last_error = NULL,
    error_category = NULL,
    result_code = NULL,
    last_batch_id = NULL,
    running_batch_id = NULL,
    last_processed_at = UNIX_TIMESTAMP(),
    updated_at = UNIX_TIMESTAMP()
WHERE id IN (
    SELECT id
    FROM (
        SELECT sr.id
        FROM song_resources sr
        JOIN song_source_refs r ON r.id = sr.song_source_ref_id
        JOIN songs s ON s.id = r.song_db_id
        WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
          AND (? = 0 OR sr.id = ?)
          AND sr.status IN (?, ?, ?, ?, ?)
          AND sr.status <> 4
          AND (? = '' OR s.title LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR r.source_song_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR r.source_content_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%'))
          AND (? = '' OR sr.resource_kind = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
          AND (? = '' OR sr.format_type = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
          AND NOT (sr.local_skip_generation=sr.content_generation AND sr.local_skip_target_revision=(SELECT target_revision FROM resource_destination_states WHERE destination='local') AND sr.local_skip_baseline<>'' AND sr.local_skip_reason<>'')
          AND NOT EXISTS (SELECT 1 FROM resource_baseline_blocks blocked WHERE blocked.owner_type='song' AND blocked.resource_id=sr.id AND blocked.content_generation=sr.content_generation)
          AND NOT EXISTS (SELECT 1 FROM resource_transfer_slots slot WHERE slot.owner_type='song' AND slot.resource_id=sr.id)
          AND NOT EXISTS (SELECT 1 FROM resource_cloud_receipts receipt WHERE receipt.owner_type='song' AND receipt.resource_id=sr.id AND (receipt.task_state IN ('running','uncertain') OR receipt.attempt IS NOT NULL))
        ORDER BY sr.updated_at ASC, sr.id ASC
        LIMIT ?
    ) candidates
)
-- 在实际更新时重核，候选列表不是取消或接管持久任务的授权。
  AND song_resources.status<>4
  AND NOT (song_resources.local_skip_generation=song_resources.content_generation AND song_resources.local_skip_target_revision=(SELECT target_revision FROM resource_destination_states WHERE destination='local') AND song_resources.local_skip_baseline<>'' AND song_resources.local_skip_reason<>'')
  AND NOT EXISTS (SELECT 1 FROM resource_baseline_blocks blocked WHERE blocked.owner_type='song' AND blocked.resource_id=song_resources.id AND blocked.content_generation=song_resources.content_generation)
  AND NOT EXISTS (SELECT 1 FROM resource_transfer_slots slot WHERE slot.owner_type='song' AND slot.resource_id=song_resources.id)
  AND NOT EXISTS (SELECT 1 FROM resource_cloud_receipts receipt WHERE receipt.owner_type='song' AND receipt.resource_id=song_resources.id AND (receipt.task_state IN ('running','uncertain') OR receipt.attempt IS NOT NULL))
