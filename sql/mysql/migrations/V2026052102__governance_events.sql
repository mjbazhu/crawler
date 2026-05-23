CREATE TABLE IF NOT EXISTS governance_events (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '治理审计事件主键',
    source VARCHAR(32) NOT NULL DEFAULT 'migu' COMMENT '音乐平台标识，例如 migu',
    action VARCHAR(64) NOT NULL COMMENT '治理动作，例如 resource.retry、batch.archive',
    target_type VARCHAR(64) NOT NULL COMMENT '治理目标类型，例如 resource、backfill、batch',
    target_id VARCHAR(128) NOT NULL DEFAULT '' COMMENT '治理目标 ID，例如 resource_id、source_album_id、batch_id',
    batch_id VARCHAR(128) COMMENT '关联批次 ID；无批次时为空',
    before_status VARCHAR(64) COMMENT '治理前状态',
    after_status VARCHAR(64) COMMENT '治理后状态',
    result_code VARCHAR(64) COMMENT '治理结果编码',
    error_category VARCHAR(64) COMMENT '失败分类',
    reason VARCHAR(512) COMMENT '治理原因或跳过原因',
    payload JSON COMMENT '脱敏上下文',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '事件创建时间，Unix 秒时间戳',
    PRIMARY KEY (id),
    KEY idx_governance_events_target (source, target_type, target_id, id),
    KEY idx_governance_events_batch (batch_id, id),
    KEY idx_governance_events_action (source, action, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Migu 控制台治理操作审计事件';

