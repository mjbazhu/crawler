-- Migu 单个音质参数入库模板。
-- 应用层遍历 songItem.audioFormats，每个音质执行一次本模板。

INSERT INTO song_source_audio_formats (
    song_source_ref_id,
    format_type,
    resource_type,
    iformat,
    aformat,
    isize,
    asize,
    created_at,
    updated_at
)
VALUES (
    @song_source_ref_id,
    @format_type,
    @format_resource_type,
    @iformat,
    @aformat,
    @isize,
    @asize,
    @now,
    @now
)
ON DUPLICATE KEY UPDATE
    resource_type = VALUES(resource_type),
    iformat = VALUES(iformat),
    aformat = VALUES(aformat),
    isize = VALUES(isize),
    asize = VALUES(asize),
    updated_at = VALUES(updated_at);

-- 音频资源没有稳定直链时，remote_url 保持 NULL；后续资源任务用 source_content_id、format_type、iformat、aformat 换取实际资源。
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
VALUES (
    @song_source_ref_id,
    'audio',
    @format_type,
    NULL,
    NULL,
    0,
    NULL,
    0,
    0,
    NULL,
    0,
    @now,
    @now
)
ON DUPLICATE KEY UPDATE
    status = IF(status IN (2, 4), status, VALUES(status)),
    updated_at = VALUES(updated_at);
