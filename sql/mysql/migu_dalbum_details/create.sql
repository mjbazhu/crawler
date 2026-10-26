-- 当前结构快照；正式升级由 migrations 管理。
CREATE TABLE IF NOT EXISTS `migu_dalbum_details` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT COMMENT 'Migu 数字专辑详情主键',
  `album_source_ref_id` bigint unsigned NOT NULL COMMENT '关联 album_source_refs.id',
  `source_material_id` varchar(64) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT 'Migu 物料 ID，例如 materialId',
  `created_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '记录创建时间，Unix 秒时间戳',
  `updated_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '记录最后更新时间，Unix 秒时间戳',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_migu_dalbum_source_ref` (`album_source_ref_id`),
  CONSTRAINT `fk_migu_dalbum_source_ref` FOREIGN KEY (`album_source_ref_id`) REFERENCES `album_source_refs` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Migu 数字专辑物料身份映射';
