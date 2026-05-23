CREATE TABLE IF NOT EXISTS songs (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '本地歌曲标题主键，供平台来源、资源、音质等子表关联使用',
    title VARCHAR(512) NOT NULL COMMENT '歌曲展示标题',
    normalized_title VARCHAR(512) NOT NULL COMMENT '用于标题池去重和检索的标准化标题，例如去空白、统一大小写后的标题',
    isrc VARCHAR(32) COMMENT '国际标准录音编码；平台未返回时为空',

    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录创建时间，Unix 秒时间戳',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录最后更新时间，Unix 秒时间戳',

    PRIMARY KEY (id),
    UNIQUE KEY uk_songs_normalized_title (normalized_title),
    KEY idx_songs_title (title),
    KEY idx_songs_isrc (isrc)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='歌曲标题池，只保存按 normalized_title 去重后的标题';
