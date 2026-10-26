-- 当前结构快照；正式升级由 migrations 管理。
CREATE TABLE IF NOT EXISTS `songs` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT COMMENT '本地歌曲主键，供平台来源、资源、音质等子表关联使用',
  `title` varchar(512) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '跨平台归一后的歌曲标题',
  `normalized_title` varchar(512) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '用于去重和检索的标准化标题，例如去空白、统一大小写后的标题',
  `isrc` varchar(32) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '国际标准录音编码；平台未返回时为空',
  `created_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '记录创建时间，Unix 秒时间戳',
  `updated_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '记录最后更新时间，Unix 秒时间戳',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_songs_normalized_title` (`normalized_title`),
  KEY `idx_songs_title` (`title`),
  KEY `idx_songs_isrc` (`isrc`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='歌曲公共基础信息表，只保存平台无关字段';
