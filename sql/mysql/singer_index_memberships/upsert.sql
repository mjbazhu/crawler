INSERT INTO singer_index_memberships (
    singer_source_ref_id,
    tab,
    index_order,
    source_tag,
    source_metric,
    created_at,
    updated_at
) VALUES (
    ?,
    ?,
    ?,
    NULLIF(?, ''),
    NULLIF(?, ''),
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
) ON DUPLICATE KEY UPDATE
    id = LAST_INSERT_ID(id),
    index_order = VALUES(index_order),
    source_tag = VALUES(source_tag),
    source_metric = VALUES(source_metric),
    updated_at = VALUES(updated_at);
