-- 当前结构快照；正式升级由 migrations 管理。
CREATE TABLE IF NOT EXISTS `singer_resources` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT COMMENT '封面资源主键',
  `singer_source_ref_id` bigint unsigned NOT NULL COMMENT '资源所属来源主键',
  `resource_kind` varchar(16) COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT 'cover' COMMENT '资源类型，当前为封面',
  `remote_url` text COLLATE utf8mb4_unicode_ci COMMENT '采集响应中的图片地址，供后续独立下载',
  `local_path` text COLLATE utf8mb4_unicode_ci COMMENT '模块内相对存储路径',
  `size_bytes` bigint unsigned NOT NULL DEFAULT '0' COMMENT '实际保存字节数',
  `checksum` varchar(128) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '完整文件校验值',
  `status` tinyint NOT NULL DEFAULT '0' COMMENT '0 待获取，2 已完成，3 失败',
  `error_category` varchar(64) COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT '' COMMENT '失败分类',
  `result_code` varchar(64) COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT '' COMMENT '处理结果码',
  `last_error` text COLLATE utf8mb4_unicode_ci COMMENT '脱敏失败原因',
  `fetched_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '成功保存时间',
  `created_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '创建时间',
  `updated_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '更新时间',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_singer_resources_owner` (`singer_source_ref_id`,`resource_kind`),
  CONSTRAINT `fk_singer_resources_owner` FOREIGN KEY (`singer_source_ref_id`) REFERENCES `singer_source_refs` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='封面地址与本地保存结果，采集阶段只登记地址';
