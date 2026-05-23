SET @crawler_schema_name := DATABASE();

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'song_artist_refs'
          AND column_name = 'singer_source_ref_id'
    ),
    'SELECT 1',
    'ALTER TABLE song_artist_refs ADD COLUMN singer_source_ref_id BIGINT UNSIGNED NULL COMMENT ''关联 singer_source_refs.id；歌手尚未入库时为空，后续可补齐'' AFTER song_source_ref_id'
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
          AND column_name = 'is_seed'
    ),
    'SELECT 1',
    'ALTER TABLE song_artist_refs ADD COLUMN is_seed TINYINT NOT NULL DEFAULT 0 COMMENT ''是否本次采集入口歌手：0 否，1 是'' AFTER is_primary'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

UPDATE song_artist_refs ar
JOIN singer_source_refs sr
  ON sr.source = ar.source
 AND sr.source_singer_id = ar.source_artist_id
SET ar.singer_source_ref_id = sr.id
WHERE ar.singer_source_ref_id IS NULL
  AND ar.source_artist_id IS NOT NULL
  AND ar.source_artist_id <> '';

UPDATE song_artist_refs
SET is_seed = 1
WHERE is_primary = 1
  AND is_seed = 0;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'song_artist_refs'
          AND index_name = 'idx_song_artist_singer_ref'
    ),
    'SELECT 1',
    'ALTER TABLE song_artist_refs ADD KEY idx_song_artist_singer_ref (singer_source_ref_id)'
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
    'ALTER TABLE song_artist_refs ADD KEY idx_song_artist_seed (source, is_seed, source_artist_id)'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.table_constraints
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'song_artist_refs'
          AND constraint_name = 'fk_song_artist_refs_singer_source'
    ),
    'SELECT 1',
    'ALTER TABLE song_artist_refs ADD CONSTRAINT fk_song_artist_refs_singer_source FOREIGN KEY (singer_source_ref_id) REFERENCES singer_source_refs (id) ON DELETE SET NULL'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'song_source_refs'
          AND index_name = 'idx_source_artist'
    ),
    'ALTER TABLE song_source_refs DROP INDEX idx_source_artist',
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
          AND column_name = 'source_primary_artist_id'
    ),
    'ALTER TABLE song_source_refs DROP COLUMN source_primary_artist_id',
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
          AND column_name = 'source_primary_artist_name'
    ),
    'ALTER TABLE song_source_refs DROP COLUMN source_primary_artist_name',
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
    2026052106,
    'song artist singer ref and song source cleanup',
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
)
ON DUPLICATE KEY UPDATE
    schema_version = GREATEST(schema_version, VALUES(schema_version)),
    schema_name = VALUES(schema_name),
    updated_at = VALUES(updated_at);
