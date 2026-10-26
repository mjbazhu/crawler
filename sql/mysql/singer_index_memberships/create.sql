-- 当前结构快照；正式升级由 migrations 管理。
CREATE TABLE IF NOT EXISTS `singer_index_memberships` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT COMMENT '歌手分类归属主键',
  `singer_source_ref_id` bigint unsigned NOT NULL COMMENT '关联 singer_source_refs.id，表示唯一平台歌手来源',
  `tab` varchar(64) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '平台歌手分类 tab，例如 huayu-nan、oumei-group',
  `index_order` int unsigned NOT NULL DEFAULT '0' COMMENT '歌手在当前分类 tab 中的顺序，从 1 开始；未知时为 0',
  `source_tag` varchar(128) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '该分类列表项的平台标签，例如 热',
  `source_metric` varchar(128) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '该分类列表项的平台热度、播放量或数量原始值',
  `created_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '记录创建时间，Unix 秒时间戳',
  `updated_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '记录最后更新时间，Unix 秒时间戳',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_singer_index_membership` (`singer_source_ref_id`,`tab`),
  KEY `idx_singer_index_tab` (`tab`,`index_order`),
  KEY `idx_singer_index_ref` (`singer_source_ref_id`),
  CONSTRAINT `fk_singer_index_memberships_ref` FOREIGN KEY (`singer_source_ref_id`) REFERENCES `singer_source_refs` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='歌手分类归属表，只记录唯一歌手来源出现在哪些平台分类 tab 中';
