-- Migu 专辑歌曲关系入库模板。
-- 依赖 05_insert_migu_song_item.sql 设置的 @song_source_ref_id 和 @source。

SET @album_source_ref_id := NULL;
SELECT id
INTO @album_source_ref_id
FROM album_source_refs
WHERE source = @source
  AND source_album_id = NULLIF(@album_ref_source_album_id, '')
LIMIT 1;

INSERT INTO album_song_refs (
    album_source_ref_id,
    song_source_ref_id,
    source,
    source_album_id,
    source_song_id,
    source_content_id,
    disc_no,
    disc_name,
    track_no,
    song_order,
    created_at,
    updated_at
)
SELECT
    @album_source_ref_id,
    @song_source_ref_id,
    @source,
    @album_ref_source_album_id,
    NULLIF(@album_ref_source_song_id, ''),
    NULLIF(@album_ref_source_content_id, ''),
    @album_ref_disc_no,
    COALESCE(@album_ref_disc_name, ''),
    @album_ref_track_no,
    @album_ref_song_order,
    @now,
    @now
WHERE @album_ref_source_album_id IS NOT NULL
  AND @album_ref_source_album_id <> ''
ON DUPLICATE KEY UPDATE
    album_source_ref_id = VALUES(album_source_ref_id),
    source_song_id = VALUES(source_song_id),
    source_content_id = VALUES(source_content_id),
    disc_no = VALUES(disc_no),
    disc_name = VALUES(disc_name),
    track_no = VALUES(track_no),
    song_order = VALUES(song_order),
    updated_at = VALUES(updated_at);
