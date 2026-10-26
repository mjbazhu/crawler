SET @crawler_schema_name := DATABASE();
SET @crawler_now := UNIX_TIMESTAMP();

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'governance_events'
          AND column_name = 'target_name'
    ),
    'SELECT 1',
    'ALTER TABLE governance_events ADD COLUMN target_name VARCHAR(512) COMMENT ''治理目标主展示名'' AFTER target_id'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'governance_events'
          AND column_name = 'target_summary'
    ),
    'SELECT 1',
    'ALTER TABLE governance_events ADD COLUMN target_summary TEXT COMMENT ''治理目标次级说明'' AFTER target_name'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'governance_events'
          AND column_name = 'target_source_id'
    ),
    'SELECT 1',
    'ALTER TABLE governance_events ADD COLUMN target_source_id VARCHAR(128) COMMENT ''治理目标平台对象 ID'' AFTER target_summary'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'governance_events'
          AND column_name = 'target_owner_name'
    ),
    'SELECT 1',
    'ALTER TABLE governance_events ADD COLUMN target_owner_name VARCHAR(512) COMMENT ''治理目标归属展示名'' AFTER target_source_id'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

INSERT INTO crawler_schema_versions (
    component,
    schema_version,
    schema_name,
    applied_at,
    updated_at
)
VALUES (
    'crawler_mysql',
    2026062601,
    'governance event target snapshot',
    @crawler_now,
    @crawler_now
)
ON DUPLICATE KEY UPDATE
    schema_version = GREATEST(schema_version, VALUES(schema_version)),
    schema_name = VALUES(schema_name),
    updated_at = VALUES(updated_at);
