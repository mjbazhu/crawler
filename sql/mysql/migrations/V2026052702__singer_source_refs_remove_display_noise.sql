SET @crawler_schema_name := DATABASE();
SET @crawler_now := UNIX_TIMESTAMP();

SET @crawler_alter_sql := IF(
    EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema = @crawler_schema_name AND table_name = 'singer_source_refs' AND column_name = 'source_view_id'),
    'ALTER TABLE singer_source_refs DROP COLUMN source_view_id',
    'SELECT 1'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema = @crawler_schema_name AND table_name = 'singer_source_refs' AND column_name = 'source_res_type'),
    'ALTER TABLE singer_source_refs DROP COLUMN source_res_type',
    'SELECT 1'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema = @crawler_schema_name AND table_name = 'singer_source_refs' AND column_name = 'source_action'),
    'ALTER TABLE singer_source_refs DROP COLUMN source_action',
    'SELECT 1'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema = @crawler_schema_name AND table_name = 'singer_source_refs' AND column_name = 'source_track'),
    'ALTER TABLE singer_source_refs DROP COLUMN source_track',
    'SELECT 1'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema = @crawler_schema_name AND table_name = 'singer_source_refs' AND column_name = 'source_display_id'),
    'ALTER TABLE singer_source_refs DROP COLUMN source_display_id',
    'SELECT 1'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema = @crawler_schema_name AND table_name = 'singer_source_refs' AND column_name = 'source_metric'),
    'ALTER TABLE singer_source_refs DROP COLUMN source_metric',
    'SELECT 1'
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
    2026052702,
    'singer source refs remove display noise',
    @crawler_now,
    @crawler_now
)
ON DUPLICATE KEY UPDATE
    schema_version = GREATEST(schema_version, VALUES(schema_version)),
    schema_name = VALUES(schema_name),
    updated_at = VALUES(updated_at);
