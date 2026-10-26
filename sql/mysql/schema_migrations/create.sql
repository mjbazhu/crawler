-- 当前结构快照；正式升级由 migrations 管理。
CREATE TABLE IF NOT EXISTS `schema_migrations` (
  `version` bigint unsigned NOT NULL COMMENT 'migration 版本号',
  `name` varchar(255) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT 'migration 名称',
  `checksum` varchar(32) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT 'migration 文件 checksum',
  `applied_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '执行时间，Unix 秒时间戳',
  `execution_ms` bigint unsigned NOT NULL DEFAULT '0' COMMENT '执行耗时，毫秒',
  PRIMARY KEY (`version`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Crawler MySQL 版本化 migration 记录';
