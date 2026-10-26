SET @crawler_schema_name := DATABASE();
SET @crawler_now := UNIX_TIMESTAMP();

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'singer_source_refs'
          AND column_name = 'source_song_total'
    ),
    'SELECT 1',
    'ALTER TABLE singer_source_refs ADD COLUMN source_song_total INT UNSIGNED NULL COMMENT ''平台声明歌曲总数，NULL 表示未知'' AFTER source_metric'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'singer_source_refs'
          AND column_name = 'source_album_total'
    ),
    'SELECT 1',
    'ALTER TABLE singer_source_refs ADD COLUMN source_album_total INT UNSIGNED NULL COMMENT ''平台声明专辑总数，NULL 表示未知'' AFTER source_song_total'
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
    2026052701,
    'singer source declared totals',
    @crawler_now,
    @crawler_now
)
ON DUPLICATE KEY UPDATE
    schema_version = GREATEST(schema_version, VALUES(schema_version)),
    schema_name = VALUES(schema_name),
    updated_at = VALUES(updated_at);
