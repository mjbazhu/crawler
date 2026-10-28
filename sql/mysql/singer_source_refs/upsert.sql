INSERT INTO singer_source_refs (
    singer_db_id,
    source,
    source_singer_id,
    source_gender,
    source_name_pinyin,
    source_country,
    created_at,
    updated_at
) VALUES (
    ?,
    ?,
    ?,
    ?,
    NULLIF(?, ''),
    NULLIF(?, ''),
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
) ON DUPLICATE KEY UPDATE
    id = LAST_INSERT_ID(id),
    singer_db_id = VALUES(singer_db_id),
    source_gender = IF(VALUES(source_gender) = 0, source_gender, VALUES(source_gender)),
    source_name_pinyin = COALESCE(VALUES(source_name_pinyin), source_name_pinyin),
    source_country = COALESCE(VALUES(source_country), source_country),
    updated_at = VALUES(updated_at);
