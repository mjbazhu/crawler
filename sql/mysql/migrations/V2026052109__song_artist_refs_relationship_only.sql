SET @crawler_schema_name := DATABASE();

SET @crawler_update_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'song_artist_refs'
          AND column_name = 'source'
    ),
    'UPDATE song_artist_refs ar JOIN singer_source_refs sr ON sr.source = ar.source AND sr.source_singer_id = ar.source_artist_id SET ar.singer_source_ref_id = sr.id WHERE ar.singer_source_ref_id IS NULL AND ar.source_artist_id IS NOT NULL AND ar.source_artist_id <> ''''',
    'SELECT 1'
);
PREPARE crawler_update_stmt FROM @crawler_update_sql;
EXECUTE crawler_update_stmt;
DEALLOCATE PREPARE crawler_update_stmt;

UPDATE song_artist_refs
SET source_artist_id = NULL
WHERE source_artist_id = '';

DROP TEMPORARY TABLE IF EXISTS crawler_song_artist_keep;

CREATE TEMPORARY TABLE crawler_song_artist_keep (
    keep_id BIGINT UNSIGNED NOT NULL,
    PRIMARY KEY (keep_id)
) ENGINE=InnoDB;

INSERT INTO crawler_song_artist_keep (keep_id)
SELECT MIN(id)
FROM song_artist_refs
WHERE singer_source_ref_id IS NOT NULL
GROUP BY song_source_ref_id, singer_source_ref_id, artist_role;

DELETE ar
FROM song_artist_refs ar
LEFT JOIN crawler_song_artist_keep keep_rows ON keep_rows.keep_id = ar.id
WHERE ar.singer_source_ref_id IS NOT NULL
  AND keep_rows.keep_id IS NULL;

TRUNCATE TABLE crawler_song_artist_keep;

INSERT INTO crawler_song_artist_keep (keep_id)
SELECT MIN(id)
FROM song_artist_refs
WHERE singer_source_ref_id IS NULL
  AND source_artist_id IS NOT NULL
GROUP BY song_source_ref_id, source_artist_id, artist_role;

DELETE ar
FROM song_artist_refs ar
LEFT JOIN crawler_song_artist_keep keep_rows ON keep_rows.keep_id = ar.id
WHERE ar.singer_source_ref_id IS NULL
  AND ar.source_artist_id IS NOT NULL
  AND keep_rows.keep_id IS NULL;

DROP TEMPORARY TABLE IF EXISTS crawler_song_artist_keep;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'song_artist_refs'
          AND index_name = 'idx_song_artist_song_source'
    ),
    'SELECT 1',
    'ALTER TABLE song_artist_refs ADD KEY idx_song_artist_song_source (song_source_ref_id)'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'song_artist_refs'
          AND index_name = 'uk_song_artist_order'
    ),
    'ALTER TABLE song_artist_refs DROP INDEX uk_song_artist_order',
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
          AND table_name = 'song_artist_refs'
          AND index_name = 'idx_song_artist_source_id'
    ),
    'ALTER TABLE song_artist_refs DROP INDEX idx_song_artist_source_id',
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
          AND table_name = 'song_artist_refs'
          AND index_name = 'idx_song_artist_name'
    ),
    'ALTER TABLE song_artist_refs DROP INDEX idx_song_artist_name',
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
          AND table_name = 'song_artist_refs'
          AND index_name = 'idx_song_artist_seed'
    ),
    'ALTER TABLE song_artist_refs DROP INDEX idx_song_artist_seed',
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
          AND table_name = 'song_artist_refs'
          AND column_name = 'source_artist_image'
    ),
    'ALTER TABLE song_artist_refs DROP COLUMN source_artist_image',
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
          AND table_name = 'song_artist_refs'
          AND column_name = 'source_name_spelling'
    ),
    'ALTER TABLE song_artist_refs DROP COLUMN source_name_spelling',
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
          AND table_name = 'song_artist_refs'
          AND column_name = 'source'
    ),
    'ALTER TABLE song_artist_refs DROP COLUMN source',
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
          AND table_name = 'song_artist_refs'
          AND index_name = 'uk_song_artist_singer'
    ),
    'SELECT 1',
    'ALTER TABLE song_artist_refs ADD UNIQUE KEY uk_song_artist_singer (song_source_ref_id, singer_source_ref_id, artist_role)'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'song_artist_refs'
          AND index_name = 'uk_song_artist_source_artist'
    ),
    'SELECT 1',
    'ALTER TABLE song_artist_refs ADD UNIQUE KEY uk_song_artist_source_artist (song_source_ref_id, source_artist_id, artist_role)'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'song_artist_refs'
          AND index_name = 'idx_song_artist_source_id'
    ),
    'SELECT 1',
    'ALTER TABLE song_artist_refs ADD KEY idx_song_artist_source_id (source_artist_id)'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'song_artist_refs'
          AND index_name = 'idx_song_artist_name'
    ),
    'SELECT 1',
    'ALTER TABLE song_artist_refs ADD KEY idx_song_artist_name (source_artist_name)'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'song_artist_refs'
          AND index_name = 'idx_song_artist_seed'
    ),
    'SELECT 1',
    'ALTER TABLE song_artist_refs ADD KEY idx_song_artist_seed (is_seed, source_artist_id)'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'song_artist_refs'
          AND index_name = 'idx_song_artist_order'
    ),
    'SELECT 1',
    'ALTER TABLE song_artist_refs ADD KEY idx_song_artist_order (song_source_ref_id, artist_role, artist_order)'
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
    2026052109,
    'song artist refs relationship only',
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
)
ON DUPLICATE KEY UPDATE
    schema_version = GREATEST(schema_version, VALUES(schema_version)),
    schema_name = VALUES(schema_name),
    updated_at = VALUES(updated_at);
