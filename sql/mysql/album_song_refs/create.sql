-- 当前结构快照；正式升级由 migrations 管理。
CREATE TABLE IF NOT EXISTS `album_song_refs` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT COMMENT '专辑歌曲关系主键',
  `album_source_ref_id` bigint unsigned NOT NULL COMMENT '关联 album_source_refs.id，写入关系前实体必须存在',
  `song_source_ref_id` bigint unsigned NOT NULL COMMENT '关联 song_source_refs.id，表示该曲目对应的平台歌曲来源',
  `disc_no` int unsigned NOT NULL DEFAULT '0' COMMENT 'Disc 序号，未知时为 0',
  `disc_name` varchar(128) COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT '' COMMENT 'Disc 原始文本，例如 Disc 1',
  `track_no` int unsigned NOT NULL DEFAULT '0' COMMENT '专辑内曲序，未知时为 0',
  `song_order` int unsigned NOT NULL DEFAULT '0' COMMENT '平台返回歌曲顺序，从 1 开始；未知时为 0',
  `created_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '记录创建时间，Unix 秒时间戳',
  `updated_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '记录最后更新时间，Unix 秒时间戳',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_album_song_relation` (`album_source_ref_id`,`song_source_ref_id`),
  KEY `idx_album_song_song` (`song_source_ref_id`),
  KEY `idx_album_song_order` (`album_source_ref_id`,`disc_no`,`track_no`,`song_order`),
  CONSTRAINT `fk_album_song_refs_album_source` FOREIGN KEY (`album_source_ref_id`) REFERENCES `album_source_refs` (`id`) ON DELETE CASCADE,
  CONSTRAINT `fk_album_song_refs_song_source` FOREIGN KEY (`song_source_ref_id`) REFERENCES `song_source_refs` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='专辑与平台歌曲关系表，保存 Disc、曲序和专辑内顺序';
