INSERT INTO song_resources (
    song_source_ref_id,
    resource_kind,
    format_type,
    status,
    error_category,
    result_code,
    last_error,
    created_at,
    updated_at
) VALUES (
    ?,
    'audio',
    '',
    4,
    'invalid_target',
    'missing_audio_formats',
    'missing audio formats from Migu metadata',
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
) ON DUPLICATE KEY UPDATE
    id = LAST_INSERT_ID(id),
    updated_at = VALUES(updated_at);
