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
