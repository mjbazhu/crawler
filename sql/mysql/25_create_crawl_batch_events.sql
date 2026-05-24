CREATE TABLE IF NOT EXISTS crawl_batch_events (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '采集事件主键',
    batch_id VARCHAR(128) NOT NULL COMMENT '关联 crawl_batches.batch_id',
    item_index INT UNSIGNED COMMENT '关联批次内目标序号；批次级事件为空',
    event_type VARCHAR(64) NOT NULL COMMENT '事件类型，详见 docs/observability/resource-batch-events.md；包含批次、资源下载、回写和关联歌词阶段',
    message TEXT COMMENT '事件描述，需脱敏',
    payload JSON COMMENT '事件附加上下文',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '事件创建时间，Unix 秒时间戳',
    PRIMARY KEY (id),
    KEY idx_crawl_batch_events_batch (batch_id, id),
    KEY idx_crawl_batch_events_type (event_type, created_at),
    CONSTRAINT fk_crawl_batch_events_batch FOREIGN KEY (batch_id) REFERENCES crawl_batches (batch_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='采集批次事件流，用于前端查看任务生命周期与失败过程';
