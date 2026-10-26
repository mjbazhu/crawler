INSERT INTO song_artist_refs (
    song_source_ref_id,
    singer_source_ref_id,
    artist_role,
    artist_order,
    is_primary,
    is_seed,
    created_at,
    updated_at
) VALUES (
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
    artist_order = IF(VALUES(artist_order) = 0, artist_order, VALUES(artist_order)),
    is_primary = GREATEST(is_primary, VALUES(is_primary)),
    is_seed = GREATEST(is_seed, VALUES(is_seed)),
    updated_at = VALUES(updated_at);
