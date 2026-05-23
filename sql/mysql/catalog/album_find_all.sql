SELECT
    title,
    0 AS album_id,
    '' AS artwork,
    primary_artist_name AS artist,
    0 AS singer_id,
    '' AS album_infos,
    0 AS listen_number,
    created_at,
    updated_at
FROM albums
ORDER BY id
LIMIT ? OFFSET ?
