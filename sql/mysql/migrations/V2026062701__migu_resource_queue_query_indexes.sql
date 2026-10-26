SET @crawler_schema_name := DATABASE();
SET @crawler_now := UNIX_TIMESTAMP();

SELECT GROUP_CONCAT(add_clause SEPARATOR ', ') INTO @crawler_alter_parts
FROM (
    SELECT 'ADD KEY idx_resource_queue_page (updated_at, id)' AS add_clause
    WHERE NOT EXISTS (
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'song_resources'
          AND index_name = 'idx_resource_queue_page'
    )
    UNION ALL
    SELECT 'ADD KEY idx_resource_queue_status_page (status, updated_at, id)' AS add_clause
    WHERE NOT EXISTS (
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'song_resources'
          AND index_name = 'idx_resource_queue_status_page'
    )
) missing_indexes;

SET @crawler_alter_sql := IF(
    @crawler_alter_parts IS NULL OR @crawler_alter_parts = '',
    'SELECT 1',
    CONCAT('ALTER TABLE song_resources ', @crawler_alter_parts)
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
    2026062701,
    'migu resource queue query indexes',
    @crawler_now,
    @crawler_now
)
ON DUPLICATE KEY UPDATE
    schema_version = GREATEST(schema_version, VALUES(schema_version)),
    schema_name = VALUES(schema_name),
    updated_at = VALUES(updated_at);
