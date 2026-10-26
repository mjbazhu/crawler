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
