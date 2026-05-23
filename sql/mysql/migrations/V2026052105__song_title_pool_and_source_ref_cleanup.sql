SET @crawler_schema_name := DATABASE();

UPDATE songs
SET normalized_title = title
WHERE normalized_title = '';

CREATE TEMPORARY TABLE crawler_song_title_keep AS
SELECT
    normalized_title,
    MIN(id) AS keep_id
FROM songs
GROUP BY normalized_title;

UPDATE song_source_refs r
JOIN songs s ON s.id = r.song_db_id
JOIN crawler_song_title_keep k ON k.normalized_title = s.normalized_title
SET r.song_db_id = k.keep_id
WHERE r.song_db_id <> k.keep_id;

DELETE s
FROM songs s
JOIN crawler_song_title_keep k ON k.normalized_title = s.normalized_title
WHERE s.id <> k.keep_id;

DROP TEMPORARY TABLE crawler_song_title_keep;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'songs'
          AND index_name = 'idx_songs_normalized_title'
    ),
    'ALTER TABLE songs DROP INDEX idx_songs_normalized_title',
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
          AND table_name = 'songs'
          AND index_name = 'uk_songs_normalized_title'
    ),
    'SELECT 1',
    'ALTER TABLE songs ADD UNIQUE KEY uk_songs_normalized_title (normalized_title)'
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
          AND column_name = 'source_title'
    ),
    'ALTER TABLE song_source_refs DROP COLUMN source_title',
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
          AND table_name = 'song_source_refs'
          AND column_name = 'copyright_type'
    ),
    'ALTER TABLE song_source_refs DROP COLUMN copyright_type',
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
          AND table_name = 'song_source_refs'
          AND column_name = 'restrict_type'
    ),
    'ALTER TABLE song_source_refs DROP COLUMN restrict_type',
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
          AND table_name = 'song_source_refs'
          AND column_name = 'product_authorize_usage'
    ),
    'ALTER TABLE song_source_refs DROP COLUMN product_authorize_usage',
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
    2026052105,
    'song title pool and song source ref cleanup',
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
)
ON DUPLICATE KEY UPDATE
    schema_version = GREATEST(schema_version, VALUES(schema_version)),
    schema_name = VALUES(schema_name),
    updated_at = VALUES(updated_at);
