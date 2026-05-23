SET @crawler_schema_name := DATABASE();

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'song_source_refs'
          AND column_name = 'source_duration_seconds'
    ),
    'SELECT 1',
    'ALTER TABLE song_source_refs ADD COLUMN source_duration_seconds INT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''平台返回的歌曲时长，单位秒；不同平台可不一致'' AFTER source_title_pinyin'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'song_source_refs'
          AND column_name = 'trc_url'
    ),
    'SELECT 1',
    'ALTER TABLE song_source_refs ADD COLUMN trc_url TEXT COMMENT ''平台翻译歌词或扩展歌词远程地址'' AFTER mrc_url'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_update_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'songs'
          AND column_name = 'duration_seconds'
    ),
    'UPDATE song_source_refs r JOIN songs s ON s.id = r.song_db_id SET r.source_duration_seconds = s.duration_seconds WHERE r.source_duration_seconds = 0',
    'SELECT 1'
);
PREPARE crawler_update_stmt FROM @crawler_update_sql;
EXECUTE crawler_update_stmt;
DEALLOCATE PREPARE crawler_update_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'songs'
          AND index_name = 'uk_songs_identity'
    ),
    'ALTER TABLE songs DROP INDEX uk_songs_identity',
    'SELECT 1'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'songs'
          AND column_name = 'duration_seconds'
    ),
    'ALTER TABLE songs DROP COLUMN duration_seconds',
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
    2026052104,
    'migu song source duration and trc resources',
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
)
ON DUPLICATE KEY UPDATE
    schema_version = GREATEST(schema_version, VALUES(schema_version)),
    schema_name = VALUES(schema_name),
    updated_at = VALUES(updated_at);
