SET @crawler_schema_name := DATABASE();
SET @crawler_now := UNIX_TIMESTAMP();

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'singer_source_refs'
          AND index_name = 'idx_singer_catalog_page'
    ),
    'SELECT 1',
    'ALTER TABLE singer_source_refs ADD KEY idx_singer_catalog_page (source, updated_at, id)'
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
          AND index_name = 'idx_singer_catalog_country_page'
    ),
    'SELECT 1',
    'ALTER TABLE singer_source_refs ADD KEY idx_singer_catalog_country_page (source, source_country, updated_at, id)'
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
          AND index_name = 'idx_singer_catalog_status_page'
    ),
    'SELECT 1',
    'ALTER TABLE singer_source_refs ADD KEY idx_singer_catalog_status_page (source, source_country, songs_sync_status, albums_sync_status, updated_at, id)'
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
          AND index_name = 'idx_song_artist_singer_song'
    ),
    'SELECT 1',
    'ALTER TABLE song_artist_refs ADD KEY idx_song_artist_singer_song (singer_source_ref_id, song_source_ref_id)'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'crawl_batch_items'
          AND index_name = 'idx_crawl_batch_items_singer_type_state'
    ),
    'SELECT 1',
    'ALTER TABLE crawl_batch_items ADD KEY idx_crawl_batch_items_singer_type_state (source, source_singer_id, item_type, state, updated_at, id)'
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
    2026052801,
    'migu singer catalog query indexes',
    @crawler_now,
    @crawler_now
)
ON DUPLICATE KEY UPDATE
    schema_version = GREATEST(schema_version, VALUES(schema_version)),
    schema_name = VALUES(schema_name),
    updated_at = VALUES(updated_at);
