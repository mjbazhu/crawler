SET @crawler_schema_name := DATABASE();

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'crawl_batches'
          AND column_name = 'archived_at'
    ),
    'SELECT 1',
    'ALTER TABLE crawl_batches ADD COLUMN archived_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''批次归档时间，0 表示未归档'' AFTER updated_at'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'crawl_batches'
          AND column_name = 'archived_reason'
    ),
    'SELECT 1',
    'ALTER TABLE crawl_batches ADD COLUMN archived_reason TEXT COMMENT ''批次归档原因，需脱敏'' AFTER archived_at'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'crawl_batches'
          AND column_name = 'parent_batch_id'
    ),
    'SELECT 1',
    'ALTER TABLE crawl_batches ADD COLUMN parent_batch_id VARCHAR(128) COMMENT ''二次治理产生的新批次来源批次 ID'' AFTER archived_reason'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'crawl_batches'
          AND index_name = 'idx_crawl_batches_archive'
    ),
    'SELECT 1',
    'ALTER TABLE crawl_batches ADD KEY idx_crawl_batches_archive (archived_at, state, updated_at)'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'crawl_batches'
          AND index_name = 'idx_crawl_batches_parent'
    ),
    'SELECT 1',
    'ALTER TABLE crawl_batches ADD KEY idx_crawl_batches_parent (parent_batch_id)'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

