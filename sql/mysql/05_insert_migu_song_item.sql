-- Migu 歌曲来源核心入库模板。
-- C++ 代码只负责绑定变量并执行本模板，不在代码中拼写业务 SQL。
-- 本模板写入 songs、song_source_refs，并生成 cover、lyric、mrc、trc 资源任务。

SET @now := UNIX_TIMESTAMP();
SET @normalized_title := COALESCE(NULLIF(@normalized_title, ''), @title);
SET @source_song_id_value := NULLIF(@source_song_id, '');
SET @source_content_id_value := NULLIF(@source_content_id, '');
SET @source_album_id_value := NULLIF(@album_id, '');
SET @source_item_key := CASE
    WHEN @source_content_id_value IS NOT NULL THEN CONCAT('content:', @source_content_id_value)
    WHEN @source_song_id_value IS NOT NULL THEN CONCAT('song:', @source_song_id_value)
    ELSE NULL
END;

SET @song_db_id := NULL;
SELECT song_db_id
INTO @song_db_id
FROM song_source_refs
WHERE source = @source
  AND source_item_key = @source_item_key
LIMIT 1;

INSERT INTO songs (
    title,
    normalized_title,
    isrc,
    created_at,
    updated_at
)
SELECT
    @title,
    @normalized_title,
    NULL,
    @now,
    @now
WHERE @song_db_id IS NULL
ON DUPLICATE KEY UPDATE
    id = LAST_INSERT_ID(id),
    title = VALUES(title),
    updated_at = VALUES(updated_at);

SET @song_db_id := COALESCE(@song_db_id, LAST_INSERT_ID());

INSERT INTO song_source_refs (
    song_db_id,
    source,
    source_item_key,
    source_song_id,
    source_content_id,
    source_resource_type,
    source_title_pinyin,
    source_duration_seconds,
    source_album_id,
    source_album_name,
    source_album_pinyin,
    cover_url,
    lyric_url,
    mrc_url,
    trc_url,
    copyright_id,
    metadata_status,
    created_at,
    updated_at
)
VALUES (
    @song_db_id,
    @source,
    @source_item_key,
    @source_song_id_value,
    @source_content_id_value,
    @source_resource_type,
    @song_pinyin,
    @source_duration_seconds,
    @source_album_id_value,
    @album_name,
    @album_pinyin,
    @cover_url,
    @lyric_url,
    @mrc_url,
    @trc_url,
    @copyright_id,
    1,
    @now,
    @now
)
ON DUPLICATE KEY UPDATE
    id = LAST_INSERT_ID(id),
    song_db_id = VALUES(song_db_id),
    source_item_key = VALUES(source_item_key),
    source_song_id = VALUES(source_song_id),
    source_content_id = VALUES(source_content_id),
    source_resource_type = VALUES(source_resource_type),
    source_title_pinyin = VALUES(source_title_pinyin),
    source_duration_seconds = VALUES(source_duration_seconds),
    source_album_id = VALUES(source_album_id),
    source_album_name = VALUES(source_album_name),
    source_album_pinyin = VALUES(source_album_pinyin),
    cover_url = VALUES(cover_url),
    lyric_url = VALUES(lyric_url),
    mrc_url = VALUES(mrc_url),
    trc_url = VALUES(trc_url),
    copyright_id = VALUES(copyright_id),
    metadata_status = VALUES(metadata_status),
    updated_at = VALUES(updated_at);

SET @song_source_ref_id := LAST_INSERT_ID();

INSERT INTO song_resources (
    song_source_ref_id,
    resource_kind,
    format_type,
    remote_url,
    local_path,
    size_bytes,
    checksum,
    status,
    retry_count,
    last_error,
    fetched_at,
    created_at,
    updated_at
)
VALUES
    (@song_source_ref_id, 'cover', '', NULLIF(@cover_url, ''), NULL, 0, NULL, 0, 0, NULL, 0, @now, @now),
    (@song_source_ref_id, 'lyric', '', NULLIF(@lyric_url, ''), NULL, 0, NULL, 0, 0, NULL, 0, @now, @now),
    (@song_source_ref_id, 'mrc', '', NULLIF(@mrc_url, ''), NULL, 0, NULL, 0, 0, NULL, 0, @now, @now),
    (@song_source_ref_id, 'trc', '', NULLIF(@trc_url, ''), NULL, 0, NULL, 0, 0, NULL, 0, @now, @now)
ON DUPLICATE KEY UPDATE
    remote_url = COALESCE(VALUES(remote_url), remote_url),
    status = IF(status = 2, status, VALUES(status)),
    updated_at = VALUES(updated_at);
