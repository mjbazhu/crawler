INSERT INTO song_source_audio_formats (
    song_source_ref_id,
    format_type,
    resource_type,
    iformat,
    aformat,
    isize,
    asize,
    created_at,
    updated_at
) VALUES (
    ?,
    ?,
    ?,
    ?,
    ?,
    ?,
    ?,
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
) ON DUPLICATE KEY UPDATE
    id = LAST_INSERT_ID(id),
    resource_type = COALESCE(NULLIF(VALUES(resource_type), ''), resource_type),
    iformat = COALESCE(NULLIF(VALUES(iformat), ''), iformat),
    aformat = COALESCE(NULLIF(VALUES(aformat), ''), aformat),
    isize = IF(VALUES(isize) = 0, isize, VALUES(isize)),
    asize = IF(VALUES(asize) = 0, asize, VALUES(asize)),
    updated_at = VALUES(updated_at);
