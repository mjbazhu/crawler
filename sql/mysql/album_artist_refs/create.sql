-- 当前结构快照；正式升级由 migrations 管理。
CREATE TABLE IF NOT EXISTS `album_artist_refs` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT COMMENT '专辑歌手关系主键',
  `album_source_ref_id` bigint unsigned NOT NULL COMMENT '平台专辑来源主键',
  `singer_source_ref_id` bigint unsigned NOT NULL COMMENT '平台歌手来源主键',
  `artist_role` varchar(64) COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT 'artist' COMMENT '歌手角色',
  `artist_order` int unsigned NOT NULL DEFAULT '0' COMMENT '平台返回顺序，0 表示未知',
  `is_primary` tinyint NOT NULL DEFAULT '0' COMMENT '是否主歌手',
  `is_seed` tinyint NOT NULL DEFAULT '0' COMMENT '是否采集入口歌手',
  `created_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '创建时间',
  `updated_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '更新时间',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_album_artist` (`album_source_ref_id`,`singer_source_ref_id`,`artist_role`),
  KEY `idx_album_artist_singer` (`singer_source_ref_id`,`album_source_ref_id`),
  CONSTRAINT `fk_album_artist_album` FOREIGN KEY (`album_source_ref_id`) REFERENCES `album_source_refs` (`id`) ON DELETE CASCADE,
  CONSTRAINT `fk_album_artist_singer` FOREIGN KEY (`singer_source_ref_id`) REFERENCES `singer_source_refs` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='专辑与歌手关系，不保存歌手属性';
