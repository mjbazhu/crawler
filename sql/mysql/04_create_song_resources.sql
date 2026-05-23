CREATE TABLE IF NOT EXISTS song_resources (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '资源记录主键',
    song_source_ref_id BIGINT UNSIGNED NOT NULL COMMENT '关联 song_source_refs.id，表示该资源来自哪个平台来源',

    resource_kind VARCHAR(32) NOT NULL COMMENT '资源类型，例如 audio、cover、lyric、mrc、trc',
    format_type VARCHAR(16) NOT NULL DEFAULT '' COMMENT '音频资源的音质档位，例如 PQ、HQ；非音频资源使用空字符串',
    remote_url TEXT COMMENT '资源远程地址；可过期地址只作临时记录，稳定重建参数应保存在来源和音质参数表',
    local_path TEXT COMMENT '资源保存到本地文件系统后的相对或绝对路径',

    size_bytes BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '本地已保存资源大小，单位字节；未知或未下载时为 0',
    checksum VARCHAR(128) COMMENT '本地资源校验值，例如 SHA-256；未计算时为空',

    status TINYINT NOT NULL DEFAULT 0 COMMENT '资源状态：0 待获取，1 获取中，2 已完成，3 失败，4 跳过',
    retry_count INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '资源获取重试次数',
    last_error TEXT COMMENT '最近一次资源获取失败原因，需脱敏后写入',
    error_category VARCHAR(64) COMMENT '资源失败分类：network、parse、persistence、copyright、resource、invalid_target、timeout、unknown',
    result_code VARCHAR(64) COMMENT '资源结果编码：ok、network_failed、parse_failed、persist_failed、invalid_target、timeout、partial_completed、failed',
    last_batch_id VARCHAR(128) COMMENT '最近处理该资源任务的批次 ID',
    running_batch_id VARCHAR(128) COMMENT '正在处理该资源任务的批次 ID',
    last_processed_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '最近治理或处理时间，Unix 秒时间戳',

    fetched_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '资源最近一次成功获取时间，Unix 秒时间戳',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录创建时间，Unix 秒时间戳',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录最后更新时间，Unix 秒时间戳',

    PRIMARY KEY (id),
    UNIQUE KEY uk_resource (song_source_ref_id, resource_kind, format_type),
    KEY idx_resource_status (status),
    KEY idx_resource_kind (resource_kind),
    KEY idx_resource_error_category (error_category, result_code, status),
    KEY idx_resource_fetch_order (status, retry_count, updated_at),
    CONSTRAINT fk_song_resources_ref FOREIGN KEY (song_source_ref_id) REFERENCES song_source_refs (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='歌曲资源表，保存封面、歌词、音频等资源的获取任务和本地保存结果';
