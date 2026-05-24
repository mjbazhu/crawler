ALTER TABLE crawl_batch_events
    MODIFY event_type VARCHAR(64) NOT NULL COMMENT '事件类型，详见 docs/observability/resource-batch-events.md；包含批次、资源下载、回写和关联歌词阶段';
