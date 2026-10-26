-- 当前结构快照；正式升级由 migrations 管理。
CREATE TABLE IF NOT EXISTS `song_source_audio_formats` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT COMMENT '平台音质参数主键',
  `song_source_ref_id` bigint unsigned NOT NULL COMMENT '关联 song_source_refs.id，表示该音质参数属于哪个平台来源',
  `format_type` varchar(16) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '平台音质档位，例如 PQ、HQ、SQ',
  `resource_type` varchar(16) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '平台资源类型，例如 Migu resourceType=2',
  `iformat` varchar(32) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT 'iOS 资源格式编码',
  `aformat` varchar(32) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT 'Android 资源格式编码',
  `isize` bigint unsigned NOT NULL DEFAULT '0' COMMENT 'iOS 资源大小，单位字节；未知为 0',
  `asize` bigint unsigned NOT NULL DEFAULT '0' COMMENT 'Android 资源大小，单位字节；未知为 0',
  `created_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '记录创建时间，Unix 秒时间戳',
  `updated_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '记录最后更新时间，Unix 秒时间戳',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_source_format` (`song_source_ref_id`,`format_type`),
  KEY `idx_audio_format_type` (`format_type`),
  CONSTRAINT `fk_song_source_audio_formats_ref` FOREIGN KEY (`song_source_ref_id`) REFERENCES `song_source_refs` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='歌曲平台音质参数表，只保存资源获取所需参数，不保存实际文件';
