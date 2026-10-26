-- 当前结构快照；正式升级由 migrations 管理。
CREATE TABLE IF NOT EXISTS `crawler_schema_versions` (
  `component` varchar(64) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT 'schema 组件名，例如 crawler_mysql',
  `schema_version` bigint unsigned NOT NULL COMMENT 'schema 版本号，使用 YYYYMMDDNN 格式递增',
  `schema_name` varchar(128) COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT '' COMMENT 'schema 名称或说明',
  `applied_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '版本写入时间，Unix 秒时间戳',
  `updated_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '记录更新时间，Unix 秒时间戳',
  PRIMARY KEY (`component`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Crawler MySQL schema 版本表';
