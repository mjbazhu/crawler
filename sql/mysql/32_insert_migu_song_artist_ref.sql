-- Migu 歌曲歌手关系入库模板。
-- 依赖 05_insert_migu_song_item.sql 设置的 @song_source_ref_id 和 @source。

INSERT INTO song_artist_refs (
    song_source_ref_id,
    singer_source_ref_id,
    source,
    source_artist_id,
    source_artist_name,
    source_artist_image,
    source_name_spelling,
    artist_role,
    artist_order,
    is_primary,
    is_seed,
    created_at,
    updated_at
)
VALUES (
    @song_source_ref_id,
    (
        SELECT ssr.id
        FROM singer_source_refs ssr
        WHERE ssr.source = @source
          AND ssr.source_singer_id = NULLIF(@artist_source_id, '')
        LIMIT 1
    ),
    @source,
    NULLIF(@artist_source_id, ''),
    COALESCE(@artist_name, ''),
    NULLIF(@artist_image, ''),
    COALESCE(@artist_name_spelling, ''),
    COALESCE(NULLIF(@artist_role, ''), 'artist'),
    @artist_order,
    @artist_is_primary,
    @artist_is_seed,
    @now,
    @now
)
ON DUPLICATE KEY UPDATE
    singer_source_ref_id = COALESCE(VALUES(singer_source_ref_id), singer_source_ref_id),
    source_artist_id = COALESCE(VALUES(source_artist_id), source_artist_id),
    source_artist_name = CASE WHEN VALUES(source_artist_name) <> '' THEN VALUES(source_artist_name) ELSE source_artist_name END,
    source_artist_image = COALESCE(VALUES(source_artist_image), source_artist_image),
    source_name_spelling = CASE WHEN VALUES(source_name_spelling) <> '' THEN VALUES(source_name_spelling) ELSE source_name_spelling END,
    is_primary = VALUES(is_primary),
    is_seed = VALUES(is_seed),
    updated_at = VALUES(updated_at);
