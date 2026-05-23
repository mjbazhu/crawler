SET @crawler_schema_name := DATABASE();

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'song_resources'
          AND column_name = 'error_category'
    ),
    'SELECT 1',
    'ALTER TABLE song_resources ADD COLUMN error_category VARCHAR(64) COMMENT ''资源失败分类：network、parse、persistence、copyright、resource、invalid_target、timeout、unknown'' AFTER last_error'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'song_resources'
          AND column_name = 'result_code'
    ),
    'SELECT 1',
    'ALTER TABLE song_resources ADD COLUMN result_code VARCHAR(64) COMMENT ''资源结果编码：ok、network_failed、parse_failed、persist_failed、invalid_target、timeout、partial_completed、failed'' AFTER error_category'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'song_resources'
          AND column_name = 'last_batch_id'
    ),
    'SELECT 1',
    'ALTER TABLE song_resources ADD COLUMN last_batch_id VARCHAR(128) COMMENT ''最近处理该资源任务的批次 ID'' AFTER result_code'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'song_resources'
          AND column_name = 'running_batch_id'
    ),
    'SELECT 1',
    'ALTER TABLE song_resources ADD COLUMN running_batch_id VARCHAR(128) COMMENT ''正在处理该资源任务的批次 ID'' AFTER last_batch_id'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'song_resources'
          AND column_name = 'last_processed_at'
    ),
    'SELECT 1',
    'ALTER TABLE song_resources ADD COLUMN last_processed_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''最近治理或处理时间，Unix 秒时间戳'' AFTER running_batch_id'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'song_resources'
          AND index_name = 'idx_resource_error_category'
    ),
    'SELECT 1',
    'ALTER TABLE song_resources ADD KEY idx_resource_error_category (error_category, result_code, status)'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

