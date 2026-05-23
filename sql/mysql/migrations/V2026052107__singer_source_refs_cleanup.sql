SET @crawler_schema_name := DATABASE();

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'singer_source_refs'
          AND column_name = 'source_name_pinyin'
    ),
    'SELECT 1',
    'ALTER TABLE singer_source_refs ADD COLUMN source_name_pinyin VARCHAR(512) COMMENT ''平台返回的歌手拼音或排序名'' AFTER source_display_id'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_update_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'singers'
          AND column_name = 'name_pinyin'
    ),
    'UPDATE singer_source_refs r JOIN singers s ON s.id = r.singer_db_id SET r.source_name_pinyin = s.name_pinyin WHERE (r.source_name_pinyin IS NULL OR r.source_name_pinyin = '''') AND s.name_pinyin IS NOT NULL AND s.name_pinyin <> ''''',
    'SELECT 1'
);
PREPARE crawler_update_stmt FROM @crawler_update_sql;
EXECUTE crawler_update_stmt;
DEALLOCATE PREPARE crawler_update_stmt;

SET @crawler_update_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'singer_source_refs'
          AND column_name = 'source_gender'
    ),
    'UPDATE singers s JOIN singer_source_refs r ON r.singer_db_id = s.id SET s.gender = r.source_gender, s.updated_at = GREATEST(s.updated_at, r.updated_at) WHERE s.gender = 0 AND r.source_gender <> 0',
    'SELECT 1'
);
PREPARE crawler_update_stmt FROM @crawler_update_sql;
EXECUTE crawler_update_stmt;
DEALLOCATE PREPARE crawler_update_stmt;

UPDATE singers
SET normalized_name = name
WHERE normalized_name = '';

DROP TEMPORARY TABLE IF EXISTS crawler_singer_keep_gender;
DROP TEMPORARY TABLE IF EXISTS crawler_singer_keep;

CREATE TEMPORARY TABLE crawler_singer_keep AS
SELECT
    normalized_name,
    MIN(id) AS keep_id
FROM singers
GROUP BY normalized_name;

CREATE TEMPORARY TABLE crawler_singer_keep_gender AS
SELECT
    k.keep_id,
    MAX(CASE WHEN s.gender <> 0 THEN s.gender ELSE 0 END) AS gender
FROM singers s
JOIN crawler_singer_keep k ON k.normalized_name = s.normalized_name
GROUP BY k.keep_id;

UPDATE singers s
JOIN crawler_singer_keep_gender g ON g.keep_id = s.id
SET
    s.gender = CASE
        WHEN s.gender = 0 AND g.gender <> 0 THEN g.gender
        ELSE s.gender
    END,
    s.updated_at = UNIX_TIMESTAMP()
WHERE s.gender = 0
  AND g.gender <> 0;

UPDATE singer_source_refs r
JOIN singers s ON s.id = r.singer_db_id
JOIN crawler_singer_keep k ON k.normalized_name = s.normalized_name
SET r.singer_db_id = k.keep_id
WHERE r.singer_db_id <> k.keep_id;

DELETE s
FROM singers s
JOIN crawler_singer_keep k ON k.normalized_name = s.normalized_name
WHERE s.id <> k.keep_id;

DROP TEMPORARY TABLE crawler_singer_keep_gender;
DROP TEMPORARY TABLE crawler_singer_keep;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'singers'
          AND index_name = 'idx_singers_normalized_name'
    ),
    'ALTER TABLE singers DROP INDEX idx_singers_normalized_name',
    'SELECT 1'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'singers'
          AND index_name = 'uk_singers_normalized_name'
    ),
    'SELECT 1',
    'ALTER TABLE singers ADD UNIQUE KEY uk_singers_normalized_name (normalized_name)'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'singer_source_refs'
          AND index_name = 'idx_singer_source_name'
    ),
    'ALTER TABLE singer_source_refs DROP INDEX idx_singer_source_name',
    'SELECT 1'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'singer_source_refs'
          AND index_name = 'idx_singer_songs_queue'
    ),
    'ALTER TABLE singer_source_refs DROP INDEX idx_singer_songs_queue',
    'SELECT 1'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'singer_source_refs'
          AND index_name = 'idx_singer_albums_queue'
    ),
    'ALTER TABLE singer_source_refs DROP INDEX idx_singer_albums_queue',
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
          AND table_name = 'singer_source_refs'
          AND column_name = 'source_name'
    ),
    'ALTER TABLE singer_source_refs DROP COLUMN source_name',
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
          AND table_name = 'singer_source_refs'
          AND column_name = 'source_gender'
    ),
    'ALTER TABLE singer_source_refs DROP COLUMN source_gender',
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
          AND table_name = 'singers'
          AND column_name = 'name_pinyin'
    ),
    'ALTER TABLE singers DROP COLUMN name_pinyin',
    'SELECT 1'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'singer_source_refs'
          AND index_name = 'idx_singer_songs_queue'
    ),
    'SELECT 1',
    'ALTER TABLE singer_source_refs ADD KEY idx_singer_songs_queue (source, songs_sync_status, source_country, updated_at)'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'singer_source_refs'
          AND index_name = 'idx_singer_albums_queue'
    ),
    'SELECT 1',
    'ALTER TABLE singer_source_refs ADD KEY idx_singer_albums_queue (source, albums_sync_status, source_country, updated_at)'
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
    2026052107,
    'singer source refs cleanup',
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
)
ON DUPLICATE KEY UPDATE
    schema_version = GREATEST(schema_version, VALUES(schema_version)),
    schema_name = VALUES(schema_name),
    updated_at = VALUES(updated_at);
