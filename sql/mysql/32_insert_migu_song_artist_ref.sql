-- Migu 歌曲歌手关系入库模板。
-- 依赖 05_insert_migu_song_item.sql 设置的 @song_source_ref_id 和 @source。

INSERT INTO song_artist_refs (
    song_source_ref_id,
    singer_source_ref_id,
    artist_role,
    artist_order,
    is_primary,
    is_seed,
    created_at,
    updated_at
)
SELECT
    @song_source_ref_id,
    ssr.id,
    COALESCE(NULLIF(@artist_role, ''), 'artist'),
    @artist_order,
    @artist_is_primary,
    @artist_is_seed,
    @now,
    @now
FROM singer_source_refs ssr
WHERE ssr.source = @source
  AND ssr.source_singer_id = NULLIF(@artist_source_id, '')
LIMIT 1
ON DUPLICATE KEY UPDATE
    artist_order = CASE WHEN VALUES(artist_order) <> 0 THEN VALUES(artist_order) ELSE artist_order END,
    is_primary = VALUES(is_primary),
    is_seed = VALUES(is_seed),
    updated_at = VALUES(updated_at);
