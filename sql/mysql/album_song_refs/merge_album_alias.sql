INSERT INTO album_song_refs (album_source_ref_id, song_source_ref_id, disc_no, disc_name, track_no, song_order, created_at, updated_at)
SELECT ?, previous.song_source_ref_id, previous.disc_no, previous.disc_name, previous.track_no, previous.song_order, previous.created_at, UNIX_TIMESTAMP()
FROM album_song_refs AS previous WHERE previous.album_source_ref_id = ?
ON DUPLICATE KEY UPDATE
    disc_no = IF(album_song_refs.disc_no = 0, VALUES(disc_no), album_song_refs.disc_no),
    disc_name = COALESCE(NULLIF(album_song_refs.disc_name, ''), VALUES(disc_name)),
    track_no = IF(album_song_refs.track_no = 0, VALUES(track_no), album_song_refs.track_no),
    song_order = IF(album_song_refs.song_order = 0, VALUES(song_order), album_song_refs.song_order),
    updated_at = VALUES(updated_at);
