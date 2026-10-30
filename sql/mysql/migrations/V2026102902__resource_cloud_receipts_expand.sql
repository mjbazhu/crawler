-- 扩展阶段：保留旧云端列，不回填、不下载、不上传，所有新目标默认维护状态。
SET @receipt_expand_ddl := CONCAT_WS(',',
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resources' AND column_name='content_generation'), NULL, 'ADD COLUMN content_generation BIGINT UNSIGNED NOT NULL DEFAULT 1'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resources' AND column_name='source_revision'), NULL, 'ADD COLUMN source_revision BIGINT UNSIGNED NOT NULL DEFAULT 1'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resources' AND column_name='reference_sha256'), NULL, 'ADD COLUMN reference_sha256 CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '''''),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resources' AND column_name='reference_size_bytes'), NULL, 'ADD COLUMN reference_size_bytes BIGINT UNSIGNED NOT NULL DEFAULT 0'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resources' AND column_name='local_content_generation'), NULL, 'ADD COLUMN local_content_generation BIGINT UNSIGNED NOT NULL DEFAULT 0'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resources' AND column_name='local_target_revision'), NULL, 'ADD COLUMN local_target_revision BIGINT UNSIGNED NOT NULL DEFAULT 0'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resources' AND column_name='local_skip_generation'), NULL, 'ADD COLUMN local_skip_generation BIGINT UNSIGNED NOT NULL DEFAULT 0'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resources' AND column_name='local_skip_target_revision'), NULL, 'ADD COLUMN local_skip_target_revision BIGINT UNSIGNED NOT NULL DEFAULT 0'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resources' AND column_name='local_skip_baseline'), NULL, 'ADD COLUMN local_skip_baseline VARCHAR(128) NOT NULL DEFAULT '''''),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resources' AND column_name='local_skip_reason'), NULL, 'ADD COLUMN local_skip_reason VARCHAR(64) NOT NULL DEFAULT '''''));
SET @receipt_expand_ddl := IF(@receipt_expand_ddl='', 'SELECT 1', CONCAT('ALTER TABLE song_resources ', @receipt_expand_ddl));
PREPARE receipt_expand_stmt FROM @receipt_expand_ddl;
EXECUTE receipt_expand_stmt;
DEALLOCATE PREPARE receipt_expand_stmt;
SET @receipt_expand_ddl := CONCAT_WS(',',
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='singer_resources' AND column_name='content_generation'), NULL, 'ADD COLUMN content_generation BIGINT UNSIGNED NOT NULL DEFAULT 1'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='singer_resources' AND column_name='source_revision'), NULL, 'ADD COLUMN source_revision BIGINT UNSIGNED NOT NULL DEFAULT 1'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='singer_resources' AND column_name='reference_sha256'), NULL, 'ADD COLUMN reference_sha256 CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '''''),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='singer_resources' AND column_name='reference_size_bytes'), NULL, 'ADD COLUMN reference_size_bytes BIGINT UNSIGNED NOT NULL DEFAULT 0'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='singer_resources' AND column_name='local_content_generation'), NULL, 'ADD COLUMN local_content_generation BIGINT UNSIGNED NOT NULL DEFAULT 0'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='singer_resources' AND column_name='local_target_revision'), NULL, 'ADD COLUMN local_target_revision BIGINT UNSIGNED NOT NULL DEFAULT 0'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='singer_resources' AND column_name='local_skip_generation'), NULL, 'ADD COLUMN local_skip_generation BIGINT UNSIGNED NOT NULL DEFAULT 0'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='singer_resources' AND column_name='local_skip_target_revision'), NULL, 'ADD COLUMN local_skip_target_revision BIGINT UNSIGNED NOT NULL DEFAULT 0'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='singer_resources' AND column_name='local_skip_baseline'), NULL, 'ADD COLUMN local_skip_baseline VARCHAR(128) NOT NULL DEFAULT '''''),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='singer_resources' AND column_name='local_skip_reason'), NULL, 'ADD COLUMN local_skip_reason VARCHAR(64) NOT NULL DEFAULT '''''));
SET @receipt_expand_ddl := IF(@receipt_expand_ddl='', 'SELECT 1', CONCAT('ALTER TABLE singer_resources ', @receipt_expand_ddl));
PREPARE receipt_expand_stmt FROM @receipt_expand_ddl;
EXECUTE receipt_expand_stmt;
DEALLOCATE PREPARE receipt_expand_stmt;
SET @receipt_expand_ddl := CONCAT_WS(',',
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_resources' AND column_name='content_generation'), NULL, 'ADD COLUMN content_generation BIGINT UNSIGNED NOT NULL DEFAULT 1'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_resources' AND column_name='source_revision'), NULL, 'ADD COLUMN source_revision BIGINT UNSIGNED NOT NULL DEFAULT 1'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_resources' AND column_name='reference_sha256'), NULL, 'ADD COLUMN reference_sha256 CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '''''),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_resources' AND column_name='reference_size_bytes'), NULL, 'ADD COLUMN reference_size_bytes BIGINT UNSIGNED NOT NULL DEFAULT 0'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_resources' AND column_name='local_content_generation'), NULL, 'ADD COLUMN local_content_generation BIGINT UNSIGNED NOT NULL DEFAULT 0'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_resources' AND column_name='local_target_revision'), NULL, 'ADD COLUMN local_target_revision BIGINT UNSIGNED NOT NULL DEFAULT 0'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_resources' AND column_name='local_skip_generation'), NULL, 'ADD COLUMN local_skip_generation BIGINT UNSIGNED NOT NULL DEFAULT 0'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_resources' AND column_name='local_skip_target_revision'), NULL, 'ADD COLUMN local_skip_target_revision BIGINT UNSIGNED NOT NULL DEFAULT 0'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_resources' AND column_name='local_skip_baseline'), NULL, 'ADD COLUMN local_skip_baseline VARCHAR(128) NOT NULL DEFAULT '''''),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_resources' AND column_name='local_skip_reason'), NULL, 'ADD COLUMN local_skip_reason VARCHAR(64) NOT NULL DEFAULT '''''));
SET @receipt_expand_ddl := IF(@receipt_expand_ddl='', 'SELECT 1', CONCAT('ALTER TABLE album_resources ', @receipt_expand_ddl));
PREPARE receipt_expand_stmt FROM @receipt_expand_ddl;
EXECUTE receipt_expand_stmt;
DEALLOCATE PREPARE receipt_expand_stmt;
SET @receipt_expand_ddl := CONCAT_WS(',',
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resource_artifacts' AND column_name='content_generation'), NULL, 'ADD COLUMN content_generation BIGINT UNSIGNED NOT NULL DEFAULT 1'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resource_artifacts' AND column_name='source_revision'), NULL, 'ADD COLUMN source_revision BIGINT UNSIGNED NOT NULL DEFAULT 1'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resource_artifacts' AND column_name='reference_sha256'), NULL, 'ADD COLUMN reference_sha256 CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '''''),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resource_artifacts' AND column_name='reference_size_bytes'), NULL, 'ADD COLUMN reference_size_bytes BIGINT UNSIGNED NOT NULL DEFAULT 0'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resource_artifacts' AND column_name='local_content_generation'), NULL, 'ADD COLUMN local_content_generation BIGINT UNSIGNED NOT NULL DEFAULT 0'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resource_artifacts' AND column_name='local_target_revision'), NULL, 'ADD COLUMN local_target_revision BIGINT UNSIGNED NOT NULL DEFAULT 0'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resource_artifacts' AND column_name='local_skip_generation'), NULL, 'ADD COLUMN local_skip_generation BIGINT UNSIGNED NOT NULL DEFAULT 0'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resource_artifacts' AND column_name='local_skip_target_revision'), NULL, 'ADD COLUMN local_skip_target_revision BIGINT UNSIGNED NOT NULL DEFAULT 0'),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resource_artifacts' AND column_name='local_skip_baseline'), NULL, 'ADD COLUMN local_skip_baseline VARCHAR(128) NOT NULL DEFAULT '''''),
    IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resource_artifacts' AND column_name='local_skip_reason'), NULL, 'ADD COLUMN local_skip_reason VARCHAR(64) NOT NULL DEFAULT '''''));
SET @receipt_expand_ddl := IF(@receipt_expand_ddl='', 'SELECT 1', CONCAT('ALTER TABLE song_resource_artifacts ', @receipt_expand_ddl));
PREPARE receipt_expand_stmt FROM @receipt_expand_ddl;
EXECUTE receipt_expand_stmt;
DEALLOCATE PREPARE receipt_expand_stmt;

-- 三个目标各保存一份当前配置与维护闸门；不保存认证。
CREATE TABLE IF NOT EXISTS resource_destination_states (
    destination VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    target_revision BIGINT UNSIGNED NOT NULL DEFAULT 1,
    target_fingerprint CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
    dispatch_state VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT 'maintenance',
    baseline_ready TINYINT UNSIGNED NOT NULL DEFAULT 0,
    baseline_manifest CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (destination),
    CONSTRAINT ck_resource_destination CHECK (destination IN ('local','baidu','gdrive')),
    CONSTRAINT ck_resource_dispatch_state CHECK (dispatch_state IN ('maintenance','active','paused'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 每种资源在每个平台一条当前任务与最后确认回执；本地回执仍在资源表。
CREATE TABLE IF NOT EXISTS resource_cloud_receipts (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    owner_type VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    resource_id BIGINT UNSIGNED NOT NULL,
    cloud_provider VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    song_resource_id BIGINT UNSIGNED DEFAULT NULL,
    singer_resource_id BIGINT UNSIGNED DEFAULT NULL,
    album_resource_id BIGINT UNSIGNED DEFAULT NULL,
    artifact_resource_id BIGINT UNSIGNED DEFAULT NULL,
    task_state VARCHAR(24) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT 'pending',
    content_generation BIGINT UNSIGNED NOT NULL DEFAULT 1,
    target_revision BIGINT UNSIGNED NOT NULL DEFAULT 1,
    claim_generation BIGINT UNSIGNED NOT NULL DEFAULT 0,
    running_batch_id VARCHAR(128) NOT NULL DEFAULT '',
    claimed_at BIGINT UNSIGNED NOT NULL DEFAULT 0,
    next_attempt_at BIGINT UNSIGNED NOT NULL DEFAULT 0,
    attempt_count INT UNSIGNED NOT NULL DEFAULT 0,
    cancel_requested TINYINT UNSIGNED NOT NULL DEFAULT 0,
    attempt JSON DEFAULT NULL,
    cloud_path TEXT DEFAULT NULL,
    size_bytes BIGINT UNSIGNED NOT NULL DEFAULT 0,
    checksum CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
    confirmed_content_generation BIGINT UNSIGNED NOT NULL DEFAULT 0,
    confirmed_target_revision BIGINT UNSIGNED NOT NULL DEFAULT 0,
    uploaded_at BIGINT UNSIGNED DEFAULT NULL,
    verified_at BIGINT UNSIGNED DEFAULT NULL,
    verification VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT 'unverified',
    verification_method VARCHAR(64) NOT NULL DEFAULT '',
    remote_hash_algorithm VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
    remote_hash_value VARCHAR(128) NOT NULL DEFAULT '',
    source_adapter VARCHAR(32) NOT NULL DEFAULT '',
    evidence JSON DEFAULT NULL,
    transfer_policy VARCHAR(24) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT 'normal',
    policy_content_generation BIGINT UNSIGNED NOT NULL DEFAULT 0,
    policy_target_revision BIGINT UNSIGNED NOT NULL DEFAULT 0,
    baseline_id VARCHAR(128) NOT NULL DEFAULT '',
    skip_reason VARCHAR(64) NOT NULL DEFAULT '',
    path_unresolved TINYINT UNSIGNED NOT NULL DEFAULT 0,
    error_category VARCHAR(64) NOT NULL DEFAULT '',
    result_code VARCHAR(64) NOT NULL DEFAULT '',
    last_error TEXT DEFAULT NULL,
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0,
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (id),
    UNIQUE KEY uk_cloud_receipt (owner_type, resource_id, cloud_provider),
    KEY idx_cloud_receipt_queue (cloud_provider, owner_type, task_state, next_attempt_at, id),
    KEY idx_cloud_receipt_baseline (baseline_id, cloud_provider, id),
    CONSTRAINT fk_cloud_receipt_song FOREIGN KEY (song_resource_id) REFERENCES song_resources(id) ON DELETE CASCADE,
    CONSTRAINT fk_cloud_receipt_singer FOREIGN KEY (singer_resource_id) REFERENCES singer_resources(id) ON DELETE CASCADE,
    CONSTRAINT fk_cloud_receipt_album FOREIGN KEY (album_resource_id) REFERENCES album_resources(id) ON DELETE CASCADE,
    CONSTRAINT fk_cloud_receipt_artifact FOREIGN KEY (artifact_resource_id) REFERENCES song_resource_artifacts(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 清单和游标持久化；终态 run 通过新关联 run 续跑，不覆写旧历史。
CREATE TABLE IF NOT EXISTS resource_cloud_backfills (
    run_id VARCHAR(128) NOT NULL,
    parent_run_id VARCHAR(128) DEFAULT NULL,
    manifest_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    database_identity VARCHAR(128) NOT NULL,
    owner_type VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    cloud_provider VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    target_revision BIGINT UNSIGNED NOT NULL,
    high_water_id BIGINT UNSIGNED NOT NULL,
    last_committed_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    strategy VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    state VARCHAR(24) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT 'paused',
    processed_count BIGINT UNSIGNED NOT NULL DEFAULT 0,
    imported_count BIGINT UNSIGNED NOT NULL DEFAULT 0,
    conflict_count BIGINT UNSIGNED NOT NULL DEFAULT 0,
    last_error TEXT DEFAULT NULL,
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0,
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (run_id),
    KEY idx_cloud_backfill_resume (state, cloud_provider, updated_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 删除资源也保留迁移证据，不绑定会级联删除的业务外键。
CREATE TABLE IF NOT EXISTS resource_cloud_migration_audit (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    run_id VARCHAR(128) NOT NULL,
    owner_type VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    resource_id BIGINT UNSIGNED NOT NULL,
    source_identity CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    content_generation BIGINT UNSIGNED NOT NULL,
    old_values JSON NOT NULL,
    disposition VARCHAR(32) NOT NULL,
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (id),
    UNIQUE KEY uk_cloud_migration_audit (run_id, owner_type, resource_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

INSERT IGNORE INTO resource_destination_states(destination,updated_at) VALUES ('local',UNIX_TIMESTAMP()),('baidu',UNIX_TIMESTAMP()),('gdrive',UNIX_TIMESTAMP());

DROP TRIGGER IF EXISTS cloud_receipt_owner_insert;
DELIMITER $$
CREATE TRIGGER cloud_receipt_owner_insert BEFORE INSERT ON resource_cloud_receipts FOR EACH ROW
BEGIN
    IF NEW.resource_id=0 OR NEW.cloud_provider NOT IN ('baidu','gdrive')
       OR ((NEW.song_resource_id IS NOT NULL)+(NEW.singer_resource_id IS NOT NULL)
          +(NEW.album_resource_id IS NOT NULL)+(NEW.artifact_resource_id IS NOT NULL))<>1
       OR NOT ((NEW.owner_type='song' AND (NEW.song_resource_id <=> NEW.resource_id))
            OR (NEW.owner_type='singer' AND (NEW.singer_resource_id <=> NEW.resource_id))
            OR (NEW.owner_type='album' AND (NEW.album_resource_id <=> NEW.resource_id))
            OR (NEW.owner_type='song_artifact' AND (NEW.artifact_resource_id <=> NEW.resource_id))) THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='invalid cloud receipt resource identity';
    END IF;
END$$
DELIMITER ;

DROP TRIGGER IF EXISTS cloud_receipt_owner_update;
DELIMITER $$
CREATE TRIGGER cloud_receipt_owner_update BEFORE UPDATE ON resource_cloud_receipts FOR EACH ROW
BEGIN
    IF NEW.resource_id=0 OR NEW.cloud_provider NOT IN ('baidu','gdrive')
       OR ((NEW.song_resource_id IS NOT NULL)+(NEW.singer_resource_id IS NOT NULL)
          +(NEW.album_resource_id IS NOT NULL)+(NEW.artifact_resource_id IS NOT NULL))<>1
       OR NOT ((NEW.owner_type='song' AND (NEW.song_resource_id <=> NEW.resource_id))
            OR (NEW.owner_type='singer' AND (NEW.singer_resource_id <=> NEW.resource_id))
            OR (NEW.owner_type='album' AND (NEW.album_resource_id <=> NEW.resource_id))
            OR (NEW.owner_type='song_artifact' AND (NEW.artifact_resource_id <=> NEW.resource_id))) THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='invalid cloud receipt resource identity';
    END IF;
END$$
DELIMITER ;

INSERT INTO crawler_schema_versions(component,schema_version,schema_name,applied_at,updated_at)
VALUES('crawler_mysql',2026102902,'resource cloud receipts expand',UNIX_TIMESTAMP(),UNIX_TIMESTAMP())
ON DUPLICATE KEY UPDATE schema_name=IF(schema_version<=VALUES(schema_version),VALUES(schema_name),schema_name),
    schema_version=GREATEST(schema_version,VALUES(schema_version)),updated_at=VALUES(updated_at);
