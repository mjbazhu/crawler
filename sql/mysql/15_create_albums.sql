CREATE TABLE IF NOT EXISTS albums (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '本地专辑主键，供平台来源表关联使用',
    title VARCHAR(512) NOT NULL COMMENT '跨平台归一后的专辑标题',
    normalized_title VARCHAR(512) NOT NULL DEFAULT '' COMMENT '用于去重和检索的标准化专辑标题',
    primary_artist_name VARCHAR(512) NOT NULL DEFAULT '' COMMENT '跨平台主艺人名称；未知时为空字符串',
    release_date VARCHAR(32) NOT NULL DEFAULT '' COMMENT '公开发行日期，使用可跨平台比较的原始日期文本；未知时为空字符串',
    identity_hash CHAR(64) NOT NULL COMMENT '专辑去重键 SHA-256，来源于 normalized_title、primary_artist_name 和 release_date',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录创建时间，Unix 秒时间戳',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录最后更新时间，Unix 秒时间戳',
    PRIMARY KEY (id),
    UNIQUE KEY uk_albums_identity_hash (identity_hash),
    KEY idx_albums_title (title),
    KEY idx_albums_normalized_title (normalized_title),
    KEY idx_albums_artist (primary_artist_name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='专辑公共基础信息表，只保存平台无关字段';
