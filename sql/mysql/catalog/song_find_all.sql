SELECT
    0 AS song_id,
    0 AS singer_id,
    0 AS album_id,
    '' AS artwork,
    title,
    '' AS artist,
    '' AS album,
    '' AS lrc,
    created_at,
    updated_at
FROM songs
ORDER BY id
LIMIT ? OFFSET ?
