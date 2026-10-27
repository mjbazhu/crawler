-- 派生产物独立于原始下载队列，正文只存在于文件中。
CREATE TABLE IF NOT EXISTS song_resource_artifacts (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '派生产物主键',
    source_resource_id BIGINT UNSIGNED NOT NULL COMMENT '原始 MRC 的 song_resources.id',
    artifact_type VARCHAR(32) NOT NULL COMMENT 'word_json 或 enhanced_lrc',
    source_checksum VARCHAR(128) NOT NULL COMMENT '本次来源校验值',
    processor_version VARCHAR(96) NOT NULL COMMENT '解码器和导出器版本',
    schema_version INT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'JSON 文档结构版本',
    local_path TEXT NULL COMMENT '存储根目录内的相对路径',
    cloud_backend VARCHAR(32) NOT NULL DEFAULT '' COMMENT '归档后端，未归档时为空',
    cloud_path TEXT NULL COMMENT '已核对的稳定云端路径',
    size_bytes BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '产物文件大小',
    checksum VARCHAR(128) NOT NULL DEFAULT '' COMMENT '产物文件校验值',
    status TINYINT NOT NULL DEFAULT 0 COMMENT '0 待处理，2 已完成，3 失败',
    result_code VARCHAR(64) NOT NULL DEFAULT '' COMMENT '稳定处理结果码，不保存密钥或来源正文',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '创建时间，Unix 秒',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '更新时间，Unix 秒',
    PRIMARY KEY (id),
    UNIQUE KEY uk_source_artifact (source_resource_id, artifact_type),
    KEY idx_artifact_status (status, updated_at),
    CONSTRAINT fk_artifact_resource FOREIGN KEY (source_resource_id) REFERENCES song_resources(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='歌词派生文件索引，不承担原始资源下载状态';
