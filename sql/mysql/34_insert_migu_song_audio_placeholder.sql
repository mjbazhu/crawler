-- 当平台没有返回 audioFormats 时，仍生成一个 audio 资源任务，方便前端看到该歌曲缺少可获取参数。

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
    '',
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
    status = IF(status = 2, status, VALUES(status)),
    updated_at = VALUES(updated_at);
