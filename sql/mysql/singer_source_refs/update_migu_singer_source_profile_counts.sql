UPDATE singer_source_refs
SET
    source_song_total = CASE WHEN ? = 1 THEN ? ELSE source_song_total END,
    source_album_total = CASE WHEN ? = 1 THEN ? ELSE source_album_total END,
    updated_at = UNIX_TIMESTAMP()
WHERE source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND source_singer_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
