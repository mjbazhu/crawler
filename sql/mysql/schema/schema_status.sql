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
    SELECT 2026102601, _utf8mb4'source_identity_and_album_count_semantics' COLLATE utf8mb4_unicode_ci, _utf8mb4'247abb66a7e3c100b1c6c56ed0d9b5c4' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102602, _utf8mb4'resource_claim_generation' COLLATE utf8mb4_unicode_ci, _utf8mb4'348b0d48b63c5d4b3e114ac16516a16f' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102603, _utf8mb4'lyric_resource_artifacts' COLLATE utf8mb4_unicode_ci, _utf8mb4'8e66529109a6db265864b5563f957e8a' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102701, _utf8mb4'migu_album_material_lookup' COLLATE utf8mb4_unicode_ci, _utf8mb4'b830cb1af262e73ec4669f5f39ae4312' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102702, _utf8mb4'singer_source_gender_ownership' COLLATE utf8mb4_unicode_ci, _utf8mb4'c920c30b5f4fd81c1c9dff0fb7167f00' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102703, _utf8mb4'metadata_cover_remote_urls' COLLATE utf8mb4_unicode_ci, _utf8mb4'75da4ef101026533d80bf7e8830b391f' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102801, _utf8mb4'singer_profile_field_cleanup' COLLATE utf8mb4_unicode_ci, _utf8mb4'0a9119000b834016660cc8601db77fba' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102802, _utf8mb4'metadata_cover_download_claims' COLLATE utf8mb4_unicode_ci, _utf8mb4'fe5df5956db30daace32711d734a390e' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102901, _utf8mb4'metadata_cover_cloud_storage' COLLATE utf8mb4_unicode_ci, _utf8mb4'ee00f3811554d6eb404813a48f58b607' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102902, _utf8mb4'resource_cloud_receipts_expand' COLLATE utf8mb4_unicode_ci, _utf8mb4'6718eaecb20a485a6a16b22fe88c4b9e' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102903, _utf8mb4'resource_transfer_jobs' COLLATE utf8mb4_unicode_ci, _utf8mb4'b975b480f4701585ee1714c098374731' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102904, _utf8mb4'resource_transfer_source_revisions' COLLATE utf8mb4_unicode_ci, _utf8mb4'12b5e247d27fe0c8281171fe70a59d44' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102905, _utf8mb4'resource_source_identity_bytes' COLLATE utf8mb4_unicode_ci, _utf8mb4'a44ce114d53df10f3778f9135336882b' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102906, _utf8mb4'preserve_legacy_local_receipts' COLLATE utf8mb4_unicode_ci, _utf8mb4'8910daaafe81b50145653997e17fc505' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102907, _utf8mb4'resource_baseline_sealing' COLLATE utf8mb4_unicode_ci, _utf8mb4'c6e2256fa2e53f999a92588f84e583fd' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102908, _utf8mb4'durable_resource_producers' COLLATE utf8mb4_unicode_ci, _utf8mb4'0b4cc5403ffbba9cd7d68c882e86fd48' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102909, _utf8mb4'legacy_resource_projection' COLLATE utf8mb4_unicode_ci, _utf8mb4'59b7f63a577393aa37ce4586ecf492d6' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102910, _utf8mb4'resource_related_discovery' COLLATE utf8mb4_unicode_ci, _utf8mb4'840cc00d07e8a0881b63c2411a8090ec' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102911, _utf8mb4'resource_discovery_and_queue_capacity' COLLATE utf8mb4_unicode_ci, _utf8mb4'faeaa35dc78d0e0c89f0c1c56faf1938' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102912, _utf8mb4'history_adoption_controls_and_cleanup' COLLATE utf8mb4_unicode_ci, _utf8mb4'81c04fa25c0c43022333b0e8459040d6' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102913, _utf8mb4'retire_resource_cloud_columns' COLLATE utf8mb4_unicode_ci, _utf8mb4'3d80d7b8b825593cf0f090f79a36a998' COLLATE utf8mb4_unicode_ci
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
    SELECT 2026102601, _utf8mb4'247abb66a7e3c100b1c6c56ed0d9b5c4' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102602, _utf8mb4'348b0d48b63c5d4b3e114ac16516a16f' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102603, _utf8mb4'8e66529109a6db265864b5563f957e8a' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102701, _utf8mb4'b830cb1af262e73ec4669f5f39ae4312' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102702, _utf8mb4'c920c30b5f4fd81c1c9dff0fb7167f00' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102703, _utf8mb4'75da4ef101026533d80bf7e8830b391f' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102801, _utf8mb4'0a9119000b834016660cc8601db77fba' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102802, _utf8mb4'fe5df5956db30daace32711d734a390e' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102901, _utf8mb4'ee00f3811554d6eb404813a48f58b607' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102902, _utf8mb4'6718eaecb20a485a6a16b22fe88c4b9e' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102903, _utf8mb4'b975b480f4701585ee1714c098374731' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102904, _utf8mb4'12b5e247d27fe0c8281171fe70a59d44' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102905, _utf8mb4'a44ce114d53df10f3778f9135336882b' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102906, _utf8mb4'8910daaafe81b50145653997e17fc505' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102907, _utf8mb4'c6e2256fa2e53f999a92588f84e583fd' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102908, _utf8mb4'0b4cc5403ffbba9cd7d68c882e86fd48' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102909, _utf8mb4'59b7f63a577393aa37ce4586ecf492d6' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102910, _utf8mb4'840cc00d07e8a0881b63c2411a8090ec' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102911, _utf8mb4'faeaa35dc78d0e0c89f0c1c56faf1938' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102912, _utf8mb4'81c04fa25c0c43022333b0e8459040d6' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT 2026102913, _utf8mb4'3d80d7b8b825593cf0f090f79a36a998' COLLATE utf8mb4_unicode_ci
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
    SELECT _utf8mb4'song_resource_artifacts' COLLATE utf8mb4_unicode_ci UNION ALL
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
    SELECT _utf8mb4'album_resources' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_receipts' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_destination_states' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_backfills' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_migration_audit' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_operations' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_tasks' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_slots' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_baselines' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_baseline_blocks' COLLATE utf8mb4_unicode_ci
 UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_profiles' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_batches' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_operations' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_related' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_runs' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_observations' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci
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
    SELECT _utf8mb4'singer_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'remote_url' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'album_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'remote_url' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'claim_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'running_batch_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'claimed_at' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'album_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'claim_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'album_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'running_batch_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'album_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'claimed_at' COLLATE utf8mb4_unicode_ci UNION ALL
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
    SELECT _utf8mb4'singer_source_refs' COLLATE utf8mb4_unicode_ci, _utf8mb4'source_gender' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_source_refs' COLLATE utf8mb4_unicode_ci, _utf8mb4'source_name_pinyin' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_source_refs' COLLATE utf8mb4_unicode_ci, _utf8mb4'source_song_total' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_source_refs' COLLATE utf8mb4_unicode_ci, _utf8mb4'source_album_total' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_index_memberships' COLLATE utf8mb4_unicode_ci, _utf8mb4'singer_source_ref_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_index_memberships' COLLATE utf8mb4_unicode_ci, _utf8mb4'tab' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_index_memberships' COLLATE utf8mb4_unicode_ci, _utf8mb4'index_order' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resource_artifacts' COLLATE utf8mb4_unicode_ci, _utf8mb4'source_resource_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resource_artifacts' COLLATE utf8mb4_unicode_ci, _utf8mb4'artifact_type' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resource_artifacts' COLLATE utf8mb4_unicode_ci, _utf8mb4'source_checksum' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resource_artifacts' COLLATE utf8mb4_unicode_ci, _utf8mb4'processor_version' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resource_artifacts' COLLATE utf8mb4_unicode_ci, _utf8mb4'schema_version' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resource_artifacts' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_path' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resource_artifacts' COLLATE utf8mb4_unicode_ci, _utf8mb4'status' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'resource_kind' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'format_type' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'error_category' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'claim_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'last_batch_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'crawl_batches' COLLATE utf8mb4_unicode_ci, _utf8mb4'archived_at' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'crawl_batches' COLLATE utf8mb4_unicode_ci, _utf8mb4'parent_batch_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'governance_events' COLLATE utf8mb4_unicode_ci, _utf8mb4'action' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'governance_events' COLLATE utf8mb4_unicode_ci, _utf8mb4'target_type' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'governance_events' COLLATE utf8mb4_unicode_ci, _utf8mb4'target_name' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'governance_events' COLLATE utf8mb4_unicode_ci, _utf8mb4'target_summary' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'governance_events' COLLATE utf8mb4_unicode_ci, _utf8mb4'target_source_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'governance_events' COLLATE utf8mb4_unicode_ci, _utf8mb4'target_owner_name' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'content_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'source_revision' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'reference_sha256' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'reference_size_bytes' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_content_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_target_revision' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_skip_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_skip_target_revision' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_skip_baseline' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_skip_reason' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'content_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'source_revision' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'reference_sha256' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'reference_size_bytes' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_content_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_target_revision' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_skip_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_skip_target_revision' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_skip_baseline' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'singer_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_skip_reason' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'album_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'content_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'album_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'source_revision' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'album_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'reference_sha256' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'album_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'reference_size_bytes' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'album_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_content_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'album_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_target_revision' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'album_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_skip_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'album_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_skip_target_revision' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'album_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_skip_baseline' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'album_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_skip_reason' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resource_artifacts' COLLATE utf8mb4_unicode_ci, _utf8mb4'content_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resource_artifacts' COLLATE utf8mb4_unicode_ci, _utf8mb4'source_revision' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resource_artifacts' COLLATE utf8mb4_unicode_ci, _utf8mb4'reference_sha256' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resource_artifacts' COLLATE utf8mb4_unicode_ci, _utf8mb4'reference_size_bytes' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resource_artifacts' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_content_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resource_artifacts' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_target_revision' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resource_artifacts' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_skip_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resource_artifacts' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_skip_target_revision' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resource_artifacts' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_skip_baseline' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resource_artifacts' COLLATE utf8mb4_unicode_ci, _utf8mb4'local_skip_reason' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_receipts' COLLATE utf8mb4_unicode_ci, _utf8mb4'cloud_provider' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_receipts' COLLATE utf8mb4_unicode_ci, _utf8mb4'transfer_policy' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_receipts' COLLATE utf8mb4_unicode_ci, _utf8mb4'confirmed_content_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_destination_states' COLLATE utf8mb4_unicode_ci, _utf8mb4'baseline_ready' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_destination_states' COLLATE utf8mb4_unicode_ci, _utf8mb4'target_revision' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_backfills' COLLATE utf8mb4_unicode_ci, _utf8mb4'last_committed_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_migration_audit' COLLATE utf8mb4_unicode_ci, _utf8mb4'source_identity' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resource_artifacts' COLLATE utf8mb4_unicode_ci, _utf8mb4'claim_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resource_artifacts' COLLATE utf8mb4_unicode_ci, _utf8mb4'running_batch_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'song_resource_artifacts' COLLATE utf8mb4_unicode_ci, _utf8mb4'claimed_at' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_operations' COLLATE utf8mb4_unicode_ci, _utf8mb4'idempotency_key' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_operations' COLLATE utf8mb4_unicode_ci, _utf8mb4'request_hash' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_operations' COLLATE utf8mb4_unicode_ci, _utf8mb4'request_snapshot' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_operations' COLLATE utf8mb4_unicode_ci, _utf8mb4'task_count' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'claim_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'native_claim_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'source_identity' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'target_fingerprint' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'attempt_path' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'attempt_checksum' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'attempt_size_bytes' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'cancel_requested' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'available_at' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_slots' COLLATE utf8mb4_unicode_ci, _utf8mb4'task_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_baselines' COLLATE utf8mb4_unicode_ci, _utf8mb4'baseline_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_baselines' COLLATE utf8mb4_unicode_ci, _utf8mb4'manifest_hash' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_baselines' COLLATE utf8mb4_unicode_ci, _utf8mb4'plan' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_baselines' COLLATE utf8mb4_unicode_ci, _utf8mb4'state' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_baselines' COLLATE utf8mb4_unicode_ci, _utf8mb4'created_at' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_baselines' COLLATE utf8mb4_unicode_ci, _utf8mb4'activated_at' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_baseline_blocks' COLLATE utf8mb4_unicode_ci, _utf8mb4'owner_type' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_baseline_blocks' COLLATE utf8mb4_unicode_ci, _utf8mb4'resource_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_baseline_blocks' COLLATE utf8mb4_unicode_ci, _utf8mb4'content_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_baseline_blocks' COLLATE utf8mb4_unicode_ci, _utf8mb4'baseline_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_baseline_blocks' COLLATE utf8mb4_unicode_ci, _utf8mb4'reason' COLLATE utf8mb4_unicode_ci
 UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'producer_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'parent_operation_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'parent_task_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'destination' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'processor_version' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'schema_version' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'state' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'claim_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'claimed_at' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'available_at' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'attempt_count' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'cancel_requested' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'timeout_ms' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'retries' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'max_bytes' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'bytes_per_second' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'result_code' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'error_category' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'child_operation_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'created_at' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'updated_at' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_profiles' COLLATE utf8mb4_unicode_ci, _utf8mb4'profile_key' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_profiles' COLLATE utf8mb4_unicode_ci, _utf8mb4'cloud_provider' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_profiles' COLLATE utf8mb4_unicode_ci, _utf8mb4'created_at' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_batches' COLLATE utf8mb4_unicode_ci, _utf8mb4'id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_batches' COLLATE utf8mb4_unicode_ci, _utf8mb4'batch_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_batches' COLLATE utf8mb4_unicode_ci, _utf8mb4'owner_type' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_batches' COLLATE utf8mb4_unicode_ci, _utf8mb4'cloud_provider' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_batches' COLLATE utf8mb4_unicode_ci, _utf8mb4'concurrency_limit' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_batches' COLLATE utf8mb4_unicode_ci, _utf8mb4'request_snapshot' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_batches' COLLATE utf8mb4_unicode_ci, _utf8mb4'terminal_result' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_batches' COLLATE utf8mb4_unicode_ci, _utf8mb4'created_at' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_batches' COLLATE utf8mb4_unicode_ci, _utf8mb4'finished_at' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_operations' COLLATE utf8mb4_unicode_ci, _utf8mb4'batch_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_operations' COLLATE utf8mb4_unicode_ci, _utf8mb4'operation_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_operations' COLLATE utf8mb4_unicode_ci, _utf8mb4'chunk_index' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_related' COLLATE utf8mb4_unicode_ci, _utf8mb4'parent_task_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_related' COLLATE utf8mb4_unicode_ci, _utf8mb4'discovered_mask' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_related' COLLATE utf8mb4_unicode_ci, _utf8mb4'claim_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_related' COLLATE utf8mb4_unicode_ci, _utf8mb4'child_operation_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_related' COLLATE utf8mb4_unicode_ci, _utf8mb4'created_at' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_related' COLLATE utf8mb4_unicode_ci, _utf8mb4'updated_at' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_operations' COLLATE utf8mb4_unicode_ci, _utf8mb4'parent_operation_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_operations' COLLATE utf8mb4_unicode_ci, _utf8mb4'parent_task_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_runs' COLLATE utf8mb4_unicode_ci,_utf8mb4'run_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_runs' COLLATE utf8mb4_unicode_ci,_utf8mb4'definition_hash' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_runs' COLLATE utf8mb4_unicode_ci,_utf8mb4'definition' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_runs' COLLATE utf8mb4_unicode_ci,_utf8mb4'owner_type' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_runs' COLLATE utf8mb4_unicode_ci,_utf8mb4'destination' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_runs' COLLATE utf8mb4_unicode_ci,_utf8mb4'high_water_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_runs' COLLATE utf8mb4_unicode_ci,_utf8mb4'cursor_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_runs' COLLATE utf8mb4_unicode_ci,_utf8mb4'state' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_runs' COLLATE utf8mb4_unicode_ci,_utf8mb4'claim_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_runs' COLLATE utf8mb4_unicode_ci,_utf8mb4'lease_until' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_runs' COLLATE utf8mb4_unicode_ci,_utf8mb4'processed' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_runs' COLLATE utf8mb4_unicode_ci,_utf8mb4'counts' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_runs' COLLATE utf8mb4_unicode_ci,_utf8mb4'result_code' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_runs' COLLATE utf8mb4_unicode_ci,_utf8mb4'active_destination' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_runs' COLLATE utf8mb4_unicode_ci,_utf8mb4'created_at' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_runs' COLLATE utf8mb4_unicode_ci,_utf8mb4'updated_at' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_observations' COLLATE utf8mb4_unicode_ci,_utf8mb4'run_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_observations' COLLATE utf8mb4_unicode_ci,_utf8mb4'resource_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_observations' COLLATE utf8mb4_unicode_ci,_utf8mb4'content_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_observations' COLLATE utf8mb4_unicode_ci,_utf8mb4'source_identity' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_observations' COLLATE utf8mb4_unicode_ci,_utf8mb4'values_digest' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_observations' COLLATE utf8mb4_unicode_ci,_utf8mb4'grade' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_observations' COLLATE utf8mb4_unicode_ci,_utf8mb4'observation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_observations' COLLATE utf8mb4_unicode_ci,_utf8mb4'observed_at' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_destination_states' COLLATE utf8mb4_unicode_ci,_utf8mb4'max_queued_transfers' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_destination_states' COLLATE utf8mb4_unicode_ci,_utf8mb4'max_queued_producers' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_destination_states' COLLATE utf8mb4_unicode_ci,_utf8mb4'queue_limits_version' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_backfills' COLLATE utf8mb4_unicode_ci,_utf8mb4'control_state' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_backfills' COLLATE utf8mb4_unicode_ci,_utf8mb4'control_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'receipt_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'owner_type' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'resource_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'content_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'cloud_provider' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'target_revision' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'target_fingerprint' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'confirmed_content_generation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'confirmed_target_revision' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'cloud_path' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'path_hash' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'object_key' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'size_bytes' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'checksum' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'verification' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'source_adapter' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'receipt_state' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'snapshot_hash' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'captured_at' COLLATE utf8mb4_unicode_ci
) expected_columns
LEFT JOIN information_schema.columns c
    ON c.table_schema = DATABASE()
   AND CAST(c.table_name AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci = expected_columns.table_name
   AND CAST(c.column_name AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci = expected_columns.column_name
UNION ALL
SELECT _utf8mb4'constraint' COLLATE utf8mb4_unicode_ci,
       expected.constraint_name COLLATE utf8mb4_unicode_ci, _utf8mb4'' COLLATE utf8mb4_unicode_ci,
       IF(actual.constraint_name IS NULL,0,1)
FROM (
    SELECT _utf8mb4'uk_cloud_receipt' COLLATE utf8mb4_unicode_ci AS constraint_name UNION ALL
    SELECT _utf8mb4'fk_cloud_receipt_song' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'fk_cloud_receipt_singer' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'fk_cloud_receipt_album' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'fk_cloud_receipt_artifact' COLLATE utf8mb4_unicode_ci
) expected LEFT JOIN information_schema.table_constraints actual
    ON actual.constraint_schema=DATABASE() AND actual.table_name='resource_cloud_receipts'
   AND CAST(actual.constraint_name AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci=expected.constraint_name
UNION ALL
SELECT _utf8mb4'constraint' COLLATE utf8mb4_unicode_ci,
       expected.trigger_name COLLATE utf8mb4_unicode_ci, _utf8mb4'' COLLATE utf8mb4_unicode_ci,
       IF(actual.trigger_name IS NULL,0,1)
FROM (
    SELECT _utf8mb4'cloud_receipt_owner_insert' COLLATE utf8mb4_unicode_ci AS trigger_name, 'INSERT' AS event_type UNION ALL
    SELECT _utf8mb4'cloud_receipt_owner_update' COLLATE utf8mb4_unicode_ci, 'UPDATE'
) expected LEFT JOIN information_schema.triggers actual
    ON actual.trigger_schema=DATABASE() AND actual.event_object_table='resource_cloud_receipts'
   AND actual.action_timing='BEFORE' AND actual.event_manipulation=expected.event_type
   AND CAST(actual.trigger_name AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci=expected.trigger_name
UNION ALL
SELECT _utf8mb4'constraint' COLLATE utf8mb4_unicode_ci,
       expected.constraint_name COLLATE utf8mb4_unicode_ci, _utf8mb4'' COLLATE utf8mb4_unicode_ci,
       IF(actual.constraint_name IS NULL,0,1)
FROM (
    SELECT _utf8mb4'resource_transfer_operations' COLLATE utf8mb4_unicode_ci AS table_name, _utf8mb4'uk_transfer_idempotency' COLLATE utf8mb4_unicode_ci AS constraint_name UNION ALL
    SELECT _utf8mb4'resource_transfer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'uk_transfer_operation_item' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'fk_transfer_task_operation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'ck_transfer_task_owner' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'ck_transfer_task_destination' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_slots' COLLATE utf8mb4_unicode_ci, _utf8mb4'uk_transfer_slot_task' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_slots' COLLATE utf8mb4_unicode_ci, _utf8mb4'fk_transfer_slot_task' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_slots' COLLATE utf8mb4_unicode_ci, _utf8mb4'ck_transfer_slot_owner' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_slots' COLLATE utf8mb4_unicode_ci, _utf8mb4'ck_transfer_slot_destination' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_baselines' COLLATE utf8mb4_unicode_ci, _utf8mb4'ck_resource_baseline_state' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_baseline_blocks' COLLATE utf8mb4_unicode_ci, _utf8mb4'ck_resource_baseline_owner' COLLATE utf8mb4_unicode_ci
 UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'uk_producer_parent_task' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'fk_producer_parent_task' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'ck_producer_destination' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'ck_producer_state' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'ck_producer_budget' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_profiles' COLLATE utf8mb4_unicode_ci, _utf8mb4'ck_legacy_profile_provider' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_batches' COLLATE utf8mb4_unicode_ci, _utf8mb4'uk_legacy_batch_id' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_batches' COLLATE utf8mb4_unicode_ci, _utf8mb4'fk_legacy_crawl_batch' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_batches' COLLATE utf8mb4_unicode_ci, _utf8mb4'ck_legacy_batch_owner' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_batches' COLLATE utf8mb4_unicode_ci, _utf8mb4'ck_legacy_batch_provider' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_batches' COLLATE utf8mb4_unicode_ci, _utf8mb4'ck_legacy_batch_concurrency' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_operations' COLLATE utf8mb4_unicode_ci, _utf8mb4'uk_legacy_operation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_operations' COLLATE utf8mb4_unicode_ci, _utf8mb4'fk_legacy_operation_batch' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_operations' COLLATE utf8mb4_unicode_ci, _utf8mb4'fk_legacy_operation_transfer' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_related' COLLATE utf8mb4_unicode_ci, _utf8mb4'fk_related_parent_task' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_related' COLLATE utf8mb4_unicode_ci, _utf8mb4'ck_related_mask' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_destination_states' COLLATE utf8mb4_unicode_ci,_utf8mb4'ck_resource_queue_limits' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_runs' COLLATE utf8mb4_unicode_ci,_utf8mb4'uk_discovery_active_destination' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_runs' COLLATE utf8mb4_unicode_ci,_utf8mb4'ck_discovery_scope' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_runs' COLLATE utf8mb4_unicode_ci,_utf8mb4'ck_discovery_state' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_observations' COLLATE utf8mb4_unicode_ci,_utf8mb4'fk_discovery_observation_run' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_observations' COLLATE utf8mb4_unicode_ci,_utf8mb4'ck_discovery_observation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_backfills' COLLATE utf8mb4_unicode_ci,_utf8mb4'ck_cloud_backfill_control_state' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'uk_cleanup_snapshot' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'ck_cleanup_identity' COLLATE utf8mb4_unicode_ci
) expected LEFT JOIN information_schema.table_constraints actual
    ON actual.constraint_schema=DATABASE()
   AND CAST(actual.table_name AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci=expected.table_name
   AND CAST(actual.constraint_name AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci=expected.constraint_name
UNION ALL
SELECT _utf8mb4'constraint' COLLATE utf8mb4_unicode_ci,
       expected.trigger_name COLLATE utf8mb4_unicode_ci,_utf8mb4'' COLLATE utf8mb4_unicode_ci,IF(actual.trigger_name IS NULL,0,1)
FROM (
    SELECT _utf8mb4'transfer_singer_source_revision' COLLATE utf8mb4_unicode_ci AS trigger_name, _utf8mb4'singer_resources' COLLATE utf8mb4_unicode_ci AS table_name, _utf8mb4'BEFORE' COLLATE utf8mb4_unicode_ci AS timing, _utf8mb4'UPDATE' COLLATE utf8mb4_unicode_ci AS event_type UNION ALL
    SELECT _utf8mb4'transfer_album_source_revision' COLLATE utf8mb4_unicode_ci, _utf8mb4'album_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'BEFORE' COLLATE utf8mb4_unicode_ci, _utf8mb4'UPDATE' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'transfer_artifact_source_revision' COLLATE utf8mb4_unicode_ci, _utf8mb4'song_resource_artifacts' COLLATE utf8mb4_unicode_ci, _utf8mb4'BEFORE' COLLATE utf8mb4_unicode_ci, _utf8mb4'UPDATE' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'transfer_song_resource_revision' COLLATE utf8mb4_unicode_ci, _utf8mb4'song_resources' COLLATE utf8mb4_unicode_ci, _utf8mb4'BEFORE' COLLATE utf8mb4_unicode_ci, _utf8mb4'UPDATE' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'transfer_song_source_revision' COLLATE utf8mb4_unicode_ci, _utf8mb4'song_source_refs' COLLATE utf8mb4_unicode_ci, _utf8mb4'AFTER' COLLATE utf8mb4_unicode_ci, _utf8mb4'UPDATE' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'transfer_song_format_insert' COLLATE utf8mb4_unicode_ci, _utf8mb4'song_source_audio_formats' COLLATE utf8mb4_unicode_ci, _utf8mb4'AFTER' COLLATE utf8mb4_unicode_ci, _utf8mb4'INSERT' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'transfer_song_format_update' COLLATE utf8mb4_unicode_ci, _utf8mb4'song_source_audio_formats' COLLATE utf8mb4_unicode_ci, _utf8mb4'AFTER' COLLATE utf8mb4_unicode_ci, _utf8mb4'UPDATE' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'transfer_song_format_delete' COLLATE utf8mb4_unicode_ci, _utf8mb4'song_source_audio_formats' COLLATE utf8mb4_unicode_ci, _utf8mb4'AFTER' COLLATE utf8mb4_unicode_ci, _utf8mb4'DELETE' COLLATE utf8mb4_unicode_ci
) expected LEFT JOIN information_schema.triggers actual
    ON actual.trigger_schema=DATABASE()
   AND CAST(actual.trigger_name AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci=expected.trigger_name
   AND CAST(actual.event_object_table AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci=expected.table_name
   AND CAST(actual.action_timing AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci=expected.timing
   AND CAST(actual.event_manipulation AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci=expected.event_type
UNION ALL
SELECT _utf8mb4'constraint' COLLATE utf8mb4_unicode_ci,CONCAT(expected.table_name,'.',expected.index_name),_utf8mb4'' COLLATE utf8mb4_unicode_ci,IF(actual.index_name IS NULL,0,1)
FROM (
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci AS table_name, _utf8mb4'idx_producer_dispatch' COLLATE utf8mb4_unicode_ci AS index_name UNION ALL
    SELECT _utf8mb4'resource_producer_tasks' COLLATE utf8mb4_unicode_ci, _utf8mb4'idx_producer_parent_operation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_legacy_batches' COLLATE utf8mb4_unicode_ci, _utf8mb4'idx_legacy_batch_active' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_transfer_operations' COLLATE utf8mb4_unicode_ci, _utf8mb4'idx_transfer_parent_operation' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_runs' COLLATE utf8mb4_unicode_ci,_utf8mb4'idx_discovery_state' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_discovery_observations' COLLATE utf8mb4_unicode_ci,_utf8mb4'idx_discovery_grade' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'idx_cleanup_provider' COLLATE utf8mb4_unicode_ci UNION ALL
    SELECT _utf8mb4'resource_cloud_cleanup_candidates' COLLATE utf8mb4_unicode_ci,_utf8mb4'idx_cleanup_resource' COLLATE utf8mb4_unicode_ci
) expected LEFT JOIN (SELECT table_name,index_name FROM information_schema.statistics WHERE table_schema=DATABASE() GROUP BY table_name,index_name) actual
 ON CAST(actual.table_name AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci=expected.table_name
 AND CAST(actual.index_name AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci=expected.index_name
UNION ALL
SELECT _utf8mb4'constraint' COLLATE utf8mb4_unicode_ci, CONCAT(_utf8mb4'retired_column:' COLLATE utf8mb4_unicode_ci,CAST(TABLE_NAME AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci,_utf8mb4'.' COLLATE utf8mb4_unicode_ci,CAST(COLUMN_NAME AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci) COLLATE utf8mb4_unicode_ci,_utf8mb4'' COLLATE utf8mb4_unicode_ci,0
FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN('song_resources','singer_resources','album_resources','song_resource_artifacts') AND COLUMN_NAME REGEXP '^cloud_';
