-- 当前结构快照；正式升级由 migrations 管理。
CREATE TABLE IF NOT EXISTS `singers` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT COMMENT '本地歌手主键，供歌曲、专辑和平台来源表关联使用',
  `name` varchar(512) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '跨平台归一后的歌手名称，例如 周杰伦',
  `normalized_name` varchar(512) COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT '' COMMENT '用于检索和弱匹配的标准化歌手名称',
  `gender` int NOT NULL DEFAULT '0' COMMENT '性别：0 未知，1 男，2 女，3 组合',
  `created_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '记录创建时间，Unix 秒时间戳',
  `updated_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '记录最后更新时间，Unix 秒时间戳',
  `status` tinyint NOT NULL DEFAULT '1' COMMENT '状态：0 待处理，1 正常，2 失效，3 失败',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_singers_normalized_name` (`normalized_name`),
  KEY `idx_singers_name` (`name`),
  KEY `idx_singers_status` (`status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='歌手公共基础信息表，只保存平台无关字段';
