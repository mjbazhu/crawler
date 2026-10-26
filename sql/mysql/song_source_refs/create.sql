-- 当前结构快照；正式升级由 migrations 管理。
CREATE TABLE IF NOT EXISTS `song_source_refs` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT COMMENT '歌曲平台来源主键，供平台音质参数和资源表关联使用',
  `song_db_id` bigint unsigned NOT NULL COMMENT '关联 songs.id，表示该平台来源属于哪一首公共歌曲',
  `source` varchar(32) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '音乐平台标识，例如 migu、qq、netease',
  `source_song_id` varchar(64) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '平台歌曲 ID，例如 Migu songId=1001416340',
  `source_content_id` varchar(64) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '平台内容或资源 ID，例如 Migu contentId/resId=600907000003024375',
  `source_resource_type` varchar(16) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '平台资源类型，例如 Migu resourceType=2',
  `source_title_pinyin` varchar(512) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '平台返回的歌曲拼音或排序名，例如 Migu songPinyin',
  `source_duration_seconds` int unsigned NOT NULL DEFAULT '0' COMMENT '平台返回的歌曲时长，单位秒；不同平台可不一致',
  `copyright_id` varchar(64) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '平台版权 ID',
  `metadata_status` tinyint NOT NULL DEFAULT '1' COMMENT '元数据状态：0 待处理，1 已获取，2 已失效，3 失败',
  `created_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '记录创建时间，Unix 秒时间戳',
  `updated_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '记录最后更新时间，Unix 秒时间戳',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_source_song` (`source`,`source_song_id`),
  UNIQUE KEY `uk_source_content` (`source`,`source_content_id`),
  KEY `idx_source_song_db_id` (`song_db_id`),
  KEY `idx_song_source_recent` (`source`,`updated_at`,`id`),
  CONSTRAINT `chk_song_source_identity` CHECK (NULLIF(TRIM(`source_song_id`),'') IS NOT NULL OR NULLIF(TRIM(`source_content_id`),'') IS NOT NULL),
  CONSTRAINT `fk_song_source_refs_song` FOREIGN KEY (`song_db_id`) REFERENCES `songs` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='歌曲平台来源表，保存不同音乐平台的外部 ID、版权和元数据入口';
