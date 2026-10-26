UPDATE song_resources
SET status = 3,
    last_error = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    error_category = 'timeout',
    result_code = 'timeout',
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
          AND sr.status = 1
          AND sr.updated_at <= UNIX_TIMESTAMP() - ?
          AND (? = '' OR s.title LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR r.source_song_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%') OR r.source_content_id LIKE CONCAT('%', CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci, '%'))
          AND (? = '' OR sr.resource_kind = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
          AND (? = '' OR sr.format_type = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
        ORDER BY sr.updated_at ASC, sr.id ASC
        LIMIT ?
    ) candidates
)
