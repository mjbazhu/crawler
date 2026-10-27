UPDATE album_source_refs canonical
JOIN album_source_refs alias ON alias.id = ?
SET canonical.source_title_pinyin = COALESCE(NULLIF(canonical.source_title_pinyin, ''), alias.source_title_pinyin),
    canonical.summary = COALESCE(NULLIF(canonical.summary, ''), alias.summary),
    canonical.source_song_total = COALESCE(canonical.source_song_total, alias.source_song_total),
    canonical.updated_at = UNIX_TIMESTAMP()
WHERE canonical.id = ?;
