CREATE TABLE IF NOT EXISTS crawler_schema_versions (
    component VARCHAR(64) NOT NULL COMMENT 'schema 组件名，例如 crawler_mysql',
    schema_version BIGINT UNSIGNED NOT NULL COMMENT 'schema 版本号，使用 YYYYMMDDNN 格式递增',
    schema_name VARCHAR(128) NOT NULL DEFAULT '' COMMENT 'schema 名称或说明',
    applied_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '版本写入时间，Unix 秒时间戳',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录更新时间，Unix 秒时间戳',
    PRIMARY KEY (component)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Crawler MySQL schema 版本表';

INSERT INTO crawler_schema_versions (
    component,
    schema_version,
    schema_name,
    applied_at,
    updated_at
)
VALUES (
    'crawler_mysql',
    2026052107,
    'singer source refs cleanup',
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
)
ON DUPLICATE KEY UPDATE
    schema_version = GREATEST(schema_version, VALUES(schema_version)),
    schema_name = VALUES(schema_name),
    updated_at = VALUES(updated_at);
