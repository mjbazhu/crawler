-- Migu 歌手来源核心入库模板。
-- C++ 代码只负责绑定变量并执行本模板，不在代码中拼写业务 SQL。
-- 本模板写入 singers 和 singer_source_refs，公共歌手名和性别只保存在 singers。

SET @now := UNIX_TIMESTAMP();
SET @name := NULLIF(@name, '');
SET @normalized_name := COALESCE(NULLIF(@normalized_name, ''), @name);
SET @source_name_pinyin_value := NULLIF(@source_name_pinyin, '');
SET @source_singer_id_value := NULLIF(@source_singer_id, '');
SET @source_item_key_value := COALESCE(
    NULLIF(@source_item_key, ''),
    CASE
        WHEN @source_singer_id_value IS NOT NULL THEN CONCAT('singer:', @source_singer_id_value)
        ELSE NULL
    END
);

SET @singer_db_id := NULL;
SELECT singer_db_id
INTO @singer_db_id
FROM singer_source_refs
WHERE source = @source
  AND source_item_key = @source_item_key_value
LIMIT 1;

SET @matched_singer_db_id := NULL;
SELECT id
INTO @matched_singer_db_id
FROM singers
WHERE normalized_name = @normalized_name
LIMIT 1;

SET @singer_db_id := COALESCE(@singer_db_id, @matched_singer_db_id);

INSERT INTO singers (
    name,
    normalized_name,
    gender,
    created_at,
    updated_at,
    status
)
SELECT
    @name,
    @normalized_name,
    @gender,
    @now,
    @now,
    1
WHERE @singer_db_id IS NULL;

SET @singer_db_id := COALESCE(@singer_db_id, LAST_INSERT_ID());

UPDATE singers
SET
    gender = CASE
        WHEN gender = 0 AND @gender <> 0 THEN @gender
        ELSE gender
    END,
    updated_at = @now
WHERE id = @singer_db_id;

INSERT INTO singer_source_refs (
    singer_db_id,
    source,
    source_item_key,
    source_singer_id,
    source_view_id,
    source_res_type,
    source_action,
    source_track,
    source_display_id,
    source_name_pinyin,
    source_tag,
    source_metric,
    source_country,
    avatar_url,
    metadata_status,
    created_at,
    updated_at
)
VALUES (
    @singer_db_id,
    @source,
    @source_item_key_value,
    @source_singer_id_value,
    NULLIF(@source_view_id, ''),
    NULLIF(@source_res_type, ''),
    NULLIF(@source_action, ''),
    NULLIF(@source_track, ''),
    NULLIF(@source_display_id, ''),
    @source_name_pinyin_value,
    NULLIF(@source_tag, ''),
    NULLIF(@source_metric, ''),
    NULLIF(@source_country, ''),
    NULLIF(@avatar_url, ''),
    1,
    @now,
    @now
)
ON DUPLICATE KEY UPDATE
    id = LAST_INSERT_ID(id),
    singer_db_id = VALUES(singer_db_id),
    source_item_key = VALUES(source_item_key),
    source_singer_id = VALUES(source_singer_id),
    source_view_id = VALUES(source_view_id),
    source_res_type = VALUES(source_res_type),
    source_action = VALUES(source_action),
    source_track = VALUES(source_track),
    source_display_id = VALUES(source_display_id),
    source_name_pinyin = VALUES(source_name_pinyin),
    source_tag = VALUES(source_tag),
    source_metric = VALUES(source_metric),
    source_country = VALUES(source_country),
    avatar_url = VALUES(avatar_url),
    metadata_status = VALUES(metadata_status),
    updated_at = VALUES(updated_at);

SET @singer_source_ref_id := LAST_INSERT_ID();
