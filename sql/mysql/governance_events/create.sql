-- 当前结构快照；正式升级由 migrations 管理。
CREATE TABLE IF NOT EXISTS `governance_events` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT COMMENT '治理审计事件主键',
  `source` varchar(32) COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT 'migu' COMMENT '音乐平台标识，例如 migu',
  `action` varchar(64) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '治理动作，例如 resource.retry、batch.archive',
  `target_type` varchar(64) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '治理目标类型，例如 resource、backfill、batch',
  `target_id` varchar(128) COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT '' COMMENT '治理目标 ID，例如 resource_id、source_album_id、batch_id',
  `target_name` varchar(512) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '治理目标主展示名',
  `target_summary` text COLLATE utf8mb4_unicode_ci COMMENT '治理目标次级说明',
  `target_source_id` varchar(128) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '治理目标平台对象 ID',
  `target_owner_name` varchar(512) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '治理目标归属展示名',
  `batch_id` varchar(128) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '关联批次 ID；无批次时为空',
  `before_status` varchar(64) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '治理前状态',
  `after_status` varchar(64) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '治理后状态',
  `result_code` varchar(64) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '治理结果编码',
  `error_category` varchar(64) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '失败分类',
  `reason` varchar(512) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '治理原因或跳过原因',
  `payload` json DEFAULT NULL COMMENT '脱敏上下文',
  `created_at` bigint unsigned NOT NULL DEFAULT '0' COMMENT '事件创建时间，Unix 秒时间戳',
  PRIMARY KEY (`id`),
  KEY `idx_governance_events_target` (`source`,`target_type`,`target_id`,`id`),
  KEY `idx_governance_events_batch` (`batch_id`,`id`),
  KEY `idx_governance_events_action` (`source`,`action`,`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Migu 控制台治理操作审计事件';
