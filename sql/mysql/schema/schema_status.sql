SELECT
    _utf8mb4'version' COLLATE utf8mb4_unicode_ci AS object_type,
    _utf8mb4'crawler_mysql' COLLATE utf8mb4_unicode_ci AS object_name,
    COALESCE(CAST(MAX(schema_version) AS CHAR CHARACTER SET utf8mb4), _utf8mb4'0') COLLATE utf8mb4_unicode_ci AS object_value,
    1 AS present
FROM crawler_schema_versions
WHERE component = 'crawler_mysql'
UNION ALL
SELECT
    _utf8mb4'schema_name' COLLATE utf8mb4_unicode_ci AS object_type,
    _utf8mb4'crawler_mysql' COLLATE utf8mb4_unicode_ci AS object_name,
    COALESCE(CAST(MAX(schema_name) AS CHAR CHARACTER SET utf8mb4), _utf8mb4'') COLLATE utf8mb4_unicode_ci AS object_value,
    1 AS present
FROM crawler_schema_versions
WHERE component = 'crawler_mysql'
UNION ALL
SELECT
    _utf8mb4'latest_applied_version' COLLATE utf8mb4_unicode_ci AS object_type,
    _utf8mb4'schema_migrations' COLLATE utf8mb4_unicode_ci AS object_name,
    COALESCE(CAST(MAX(version) AS CHAR CHARACTER SET utf8mb4), _utf8mb4'0') COLLATE utf8mb4_unicode_ci AS object_value,
    1 AS present
FROM schema_migrations
UNION ALL
SELECT
    _utf8mb4'pending_migration' COLLATE utf8mb4_unicode_ci AS object_type,
    CAST(expected.version AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci AS object_name,
    expected.name COLLATE utf8mb4_unicode_ci AS object_value,
    IF(m.version IS NULL, 1, 0) AS present
FROM (
    SELECT 2026052001 AS version, _utf8mb4'baseline_current_schema' COLLATE utf8mb4_unicode_ci AS name, _utf8mb4'01ad9701fbd24db7b62ae9b653a8428b' COLLATE utf8mb4_unicode_ci AS checksum UNION ALL
    SELECT 2026052101, _utf8mb4'resource_error_fields' COLLATE utf8mb4_unicode_ci, _utf8mb4'7f315bdb1dc542a2595bb96a2d9a0b7e' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052102, _utf8mb4'governance_events' COLLATE utf8mb4_unicode_ci, _utf8mb4'64ed79f772ff88e57873639f75e3d219' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052103, _utf8mb4'batch_archive_and_summary' COLLATE utf8mb4_unicode_ci, _utf8mb4'401abce32cffbb673ce24faa8db57eb3' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052104, _utf8mb4'song_source_duration_and_trc' COLLATE utf8mb4_unicode_ci, _utf8mb4'993e45db55b0d2a37f905c6687178ca7' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052105, _utf8mb4'song_title_pool_and_source_ref_cleanup' COLLATE utf8mb4_unicode_ci, _utf8mb4'01d9b91f9ae4ae7969991224918ee13d' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052106, _utf8mb4'song_artist_singer_ref_and_source_cleanup' COLLATE utf8mb4_unicode_ci, _utf8mb4'b20f95a834c495f13438e5fc489bae55' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052107, _utf8mb4'singer_source_refs_cleanup' COLLATE utf8mb4_unicode_ci, _utf8mb4'38ad6e61870a887626808e2bdb7c4902' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052108, _utf8mb4'singer_source_summary_recount' COLLATE utf8mb4_unicode_ci, _utf8mb4'3f330f7b48f6426d78e6978d4b6f9319' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052109, _utf8mb4'song_artist_refs_relationship_only' COLLATE utf8mb4_unicode_ci, _utf8mb4'f8b5203d1c073c39b48eaa59e3524e7d' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052110, _utf8mb4'singer_index_memberships' COLLATE utf8mb4_unicode_ci, _utf8mb4'4d0c1a71104ce83ca80670a7b2158a70' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052111, _utf8mb4'song_artist_refs_strict_singer_relation' COLLATE utf8mb4_unicode_ci, _utf8mb4'497162f32080fb050713b8bea37e8af1' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052401, _utf8mb4'crawl_batch_events_event_type_comment' COLLATE utf8mb4_unicode_ci, _utf8mb4'1f414eb18631d03dd1b8e48ed67a2db1' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052402, _utf8mb4'migu_song_cover_url_prefix' COLLATE utf8mb4_unicode_ci, _utf8mb4'ae8670caa76d7e369c827510c67cf9a2' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052403, _utf8mb4'migu_text_resource_queue_cleanup' COLLATE utf8mb4_unicode_ci, _utf8mb4'7727903016da550f3dec22c3b76b8a93' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052501, _utf8mb4'song_resource_cloud_upload_fields' COLLATE utf8mb4_unicode_ci, _utf8mb4'7c3b3f8aafd8c94a17d02666d31d124e' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052601, _utf8mb4'migu_resource_service_cover_url_prefix' COLLATE utf8mb4_unicode_ci, _utf8mb4'311a0732b49b4affaa9e3e4456af6c9b' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052701, _utf8mb4'singer_source_declared_totals' COLLATE utf8mb4_unicode_ci, _utf8mb4'a68ed2c6e3b67a192567746b837416a5' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052702, _utf8mb4'singer_source_refs_remove_display_noise' COLLATE utf8mb4_unicode_ci, _utf8mb4'45da54be22609915b773ef86e52203a1' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052801, _utf8mb4'migu_singer_catalog_query_indexes' COLLATE utf8mb4_unicode_ci, _utf8mb4'06d46411a1907248f0c4afe20619ad9c' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026062601, _utf8mb4'governance_event_target_snapshot' COLLATE utf8mb4_unicode_ci, _utf8mb4'030b0f8fee504853e310cbdb78c809c7' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026062701, _utf8mb4'migu_resource_queue_query_indexes' COLLATE utf8mb4_unicode_ci, _utf8mb4'6cb2b369c0880891cb54a8a8446d6b2d' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102501, _utf8mb4'normalize_music_table_ownership' COLLATE utf8mb4_unicode_ci, _utf8mb4'7852e56e1bffd18dbd6785882018cf39' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102502, _utf8mb4'optimize_normalized_catalog_queries' COLLATE utf8mb4_unicode_ci, _utf8mb4'55b4ebb3fd1a1787ed75a0e4ec8e912b' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102601, _utf8mb4'source_identity_and_album_count_semantics' COLLATE utf8mb4_unicode_ci, _utf8mb4'247abb66a7e3c100b1c6c56ed0d9b5c4' COLLATE utf8mb4_unicode_ci
) expected
LEFT JOIN schema_migrations m ON m.version = expected.version
UNION ALL
SELECT
    _utf8mb4'checksum_mismatch' COLLATE utf8mb4_unicode_ci AS object_type,
    CAST(expected.version AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci AS object_name,
    CONCAT(_utf8mb4'expected=' COLLATE utf8mb4_unicode_ci, expected.checksum, _utf8mb4', actual=' COLLATE utf8mb4_unicode_ci, COALESCE(m.checksum, _utf8mb4'' COLLATE utf8mb4_unicode_ci)) COLLATE utf8mb4_unicode_ci AS object_value,
    IF(m.version IS NOT NULL AND m.checksum <> expected.checksum, 1, 0) AS present
FROM (
    SELECT 2026052001 AS version, _utf8mb4'01ad9701fbd24db7b62ae9b653a8428b' COLLATE utf8mb4_unicode_ci AS checksum UNION ALL
    SELECT 2026052101, _utf8mb4'7f315bdb1dc542a2595bb96a2d9a0b7e' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052102, _utf8mb4'64ed79f772ff88e57873639f75e3d219' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052103, _utf8mb4'401abce32cffbb673ce24faa8db57eb3' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052104, _utf8mb4'993e45db55b0d2a37f905c6687178ca7' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052105, _utf8mb4'01d9b91f9ae4ae7969991224918ee13d' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052106, _utf8mb4'b20f95a834c495f13438e5fc489bae55' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052107, _utf8mb4'38ad6e61870a887626808e2bdb7c4902' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052108, _utf8mb4'3f330f7b48f6426d78e6978d4b6f9319' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052109, _utf8mb4'f8b5203d1c073c39b48eaa59e3524e7d' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052110, _utf8mb4'4d0c1a71104ce83ca80670a7b2158a70' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052111, _utf8mb4'497162f32080fb050713b8bea37e8af1' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052401, _utf8mb4'1f414eb18631d03dd1b8e48ed67a2db1' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052402, _utf8mb4'ae8670caa76d7e369c827510c67cf9a2' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052403, _utf8mb4'7727903016da550f3dec22c3b76b8a93' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052501, _utf8mb4'7c3b3f8aafd8c94a17d02666d31d124e' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052601, _utf8mb4'311a0732b49b4affaa9e3e4456af6c9b' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052701, _utf8mb4'a68ed2c6e3b67a192567746b837416a5' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052702, _utf8mb4'45da54be22609915b773ef86e52203a1' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026052801, _utf8mb4'06d46411a1907248f0c4afe20619ad9c' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026062601, _utf8mb4'030b0f8fee504853e310cbdb78c809c7' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026062701, _utf8mb4'6cb2b369c0880891cb54a8a8446d6b2d' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102501, _utf8mb4'7852e56e1bffd18dbd6785882018cf39' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102502, _utf8mb4'55b4ebb3fd1a1787ed75a0e4ec8e912b' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102601, _utf8mb4'247abb66a7e3c100b1c6c56ed0d9b5c4' COLLATE utf8mb4_unicode_ci
) expected
LEFT JOIN schema_migrations m ON m.version = expected.version
UNION ALL
SELECT
    _utf8mb4'table' COLLATE utf8mb4_unicode_ci AS object_type,
    expected_tables.table_name COLLATE utf8mb4_unicode_ci AS object_name,
    _utf8mb4'' COLLATE utf8mb4_unicode_ci AS object_value,
    IF(t.table_name IS NULL, 0, 1) AS present
FROM (
    SELECT _utf8mb4'songs' COLLATE utf8mb4_unicode_ci AS table_name UNION ALL
    SELECT _utf8mb4'song_source_refs' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_source_audio_formats' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_artist_refs' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'album_song_refs' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singers' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_source_refs' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_index_memberships' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'albums' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'album_source_refs' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'migu_dalbum_details' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'crawl_batches' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'crawl_batch_items' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'crawl_batch_events' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'crawler_schema_versions' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'schema_migrations' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'governance_events' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'album_artist_refs' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_resources' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'album_resources' COLLATE utf8mb4_unicode_ci
) expected_tables
LEFT JOIN information_schema.tables t
    ON t.table_schema = DATABASE()
   AND CAST(t.table_name AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci = expected_tables.table_name
UNION ALL
SELECT
    _utf8mb4'column' COLLATE utf8mb4_unicode_ci AS object_type,
    CONCAT(expected_columns.table_name, _utf8mb4'.' COLLATE utf8mb4_unicode_ci, expected_columns.column_name) COLLATE utf8mb4_unicode_ci AS object_name,
    _utf8mb4'' COLLATE utf8mb4_unicode_ci AS object_value,
    IF(c.column_name IS NULL, 0, 1) AS present
FROM (
    SELECT _utf8mb4'song_source_refs' COLLATE utf8mb4_unicode_ci AS table_name, _utf8mb4'source_duration_seconds' COLLATE utf8mb4_unicode_ci AS column_name UNION ALL
    SELECT _utf8mb4'album_source_refs' COLLATE utf8mb4_unicode_ci, _utf8mb4'source_title_pinyin' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_artist_refs' COLLATE utf8mb4_unicode_ci, _utf8mb4'artist_order' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_artist_refs' COLLATE utf8mb4_unicode_ci, _utf8mb4'is_primary' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'album_song_refs' COLLATE utf8mb4_unicode_ci, _utf8mb4'album_source_ref_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'album_song_refs' COLLATE utf8mb4_unicode_ci, _utf8mb4'disc_no' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'album_song_refs' COLLATE utf8mb4_unicode_ci, _utf8mb4'track_no' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'album_song_refs' COLLATE utf8mb4_unicode_ci, _utf8mb4'song_order' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_source_audio_formats' COLLATE utf8mb4_unicode_ci, _utf8mb4'resource_type' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_source_audio_formats' COLLATE utf8mb4_unicode_ci, _utf8mb4'iformat' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_source_audio_formats' COLLATE utf8mb4_unicode_ci, _utf8mb4'aformat' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_artist_refs' COLLATE utf8mb4_unicode_ci, _utf8mb4'singer_source_ref_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_artist_refs' COLLATE utf8mb4_unicode_ci, _utf8mb4'is_seed' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_source_refs' COLLATE utf8mb4_unicode_ci, _utf8mb4'source_name_pinyin' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_source_refs' COLLATE utf8mb4_unicode_ci, _utf8mb4'source_song_total' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_source_refs' COLLATE utf8mb4_unicode_ci, _utf8mb4'source_album_total' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_index_memberships' COLLATE utf8mb4_unicode_ci, _utf8mb4'singer_source_ref_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_index_memberships' COLLATE utf8mb4_unicode_ci, _utf8mb4'tab' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_index_memberships' COLLATE utf8mb4_unicode_ci, _utf8mb4'index_order' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'resource_kind' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'format_type' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'error_category' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'last_batch_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'cloud_backend' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'cloud_path' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'cloud_uploaded_at' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'crawl_batches' COLLATE utf8mb4_unicode_ci, _utf8mb4'archived_at' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'crawl_batches' COLLATE utf8mb4_unicode_ci, _utf8mb4'parent_batch_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'governance_events' COLLATE utf8mb4_unicode_ci, _utf8mb4'action' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'governance_events' COLLATE utf8mb4_unicode_ci, _utf8mb4'target_type' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'governance_events' COLLATE utf8mb4_unicode_ci, _utf8mb4'target_name' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'governance_events' COLLATE utf8mb4_unicode_ci, _utf8mb4'target_summary' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'governance_events' COLLATE utf8mb4_unicode_ci, _utf8mb4'target_source_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'governance_events' COLLATE utf8mb4_unicode_ci, _utf8mb4'target_owner_name' COLLATE utf8mb4_unicode_ci
) expected_columns
LEFT JOIN information_schema.columns c
    ON c.table_schema = DATABASE()
   AND CAST(c.table_name AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci = expected_columns.table_name
   AND CAST(c.column_name AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci = expected_columns.column_name;
