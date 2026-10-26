INSERT INTO album_song_refs (
    album_source_ref_id,
    song_source_ref_id,
    disc_no,
    disc_name,
    track_no,
    song_order,
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
    disc_no = IF(VALUES(disc_no) = 0, disc_no, VALUES(disc_no)),
    disc_name = COALESCE(NULLIF(VALUES(disc_name), ''), disc_name),
    track_no = IF(VALUES(track_no) = 0, track_no, VALUES(track_no)),
    song_order = IF(VALUES(song_order) = 0, song_order, VALUES(song_order)),
    updated_at = VALUES(updated_at);
