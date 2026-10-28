INSERT INTO album_artist_refs (album_source_ref_id, singer_source_ref_id, artist_role, artist_order, is_primary, is_seed, created_at, updated_at)
SELECT ?, previous.singer_source_ref_id, previous.artist_role, previous.artist_order, previous.is_primary, previous.is_seed, previous.created_at, UNIX_TIMESTAMP()
FROM album_artist_refs AS previous WHERE previous.album_source_ref_id = ?
ON DUPLICATE KEY UPDATE
    artist_order = IF(album_artist_refs.artist_order = 0, VALUES(artist_order), album_artist_refs.artist_order),
    is_primary = GREATEST(album_artist_refs.is_primary, VALUES(is_primary)),
    is_seed = GREATEST(album_artist_refs.is_seed, VALUES(is_seed)),
    updated_at = VALUES(updated_at);
