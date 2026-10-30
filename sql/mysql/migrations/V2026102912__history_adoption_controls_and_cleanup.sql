-- 显式历史导入控制和删除前只读清理证据；旧迁移保持不变。
DROP PROCEDURE IF EXISTS crawler_history_control_expand;
DELIMITER $$
CREATE PROCEDURE crawler_history_control_expand()
BEGIN
 IF NOT EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='resource_cloud_backfills' AND column_name='control_state') THEN
  ALTER TABLE resource_cloud_backfills ADD COLUMN control_state VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT 'active';
 END IF;
 IF NOT EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='resource_cloud_backfills' AND column_name='control_generation') THEN
  ALTER TABLE resource_cloud_backfills ADD COLUMN control_generation BIGINT UNSIGNED NOT NULL DEFAULT 0;
 END IF;
 IF NOT EXISTS(SELECT 1 FROM information_schema.table_constraints WHERE constraint_schema=DATABASE() AND table_name='resource_cloud_backfills' AND constraint_name='ck_cloud_backfill_control_state') THEN
  ALTER TABLE resource_cloud_backfills ADD CONSTRAINT ck_cloud_backfill_control_state CHECK(control_state IN('active','paused','cancelled'));
 END IF;
END$$
CALL crawler_history_control_expand()$$
DROP PROCEDURE crawler_history_control_expand$$
DELIMITER ;
-- 删除前显式捕获的不可变证据；无资源外键，不随资源级联删除。
CREATE TABLE IF NOT EXISTS resource_cloud_cleanup_candidates (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    receipt_id BIGINT UNSIGNED NOT NULL,
    owner_type VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    resource_id BIGINT UNSIGNED NOT NULL,
    content_generation BIGINT UNSIGNED NOT NULL,
    cloud_provider VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    target_revision BIGINT UNSIGNED NOT NULL,
    target_fingerprint CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    confirmed_content_generation BIGINT UNSIGNED NOT NULL,
    confirmed_target_revision BIGINT UNSIGNED NOT NULL,
    cloud_path TEXT NOT NULL,
    path_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    object_key VARCHAR(4096) COLLATE utf8mb4_bin NOT NULL DEFAULT '',
    size_bytes BIGINT UNSIGNED NOT NULL,
    checksum CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
    verification VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    source_adapter VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    receipt_state VARCHAR(24) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    snapshot_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    captured_at BIGINT UNSIGNED NOT NULL,
    PRIMARY KEY (id),
    UNIQUE KEY uk_cleanup_snapshot (receipt_id,snapshot_hash),
    KEY idx_cleanup_provider (cloud_provider,id),
    KEY idx_cleanup_resource (owner_type,resource_id,id),
    CONSTRAINT ck_cleanup_identity CHECK (receipt_id>0 AND resource_id>0 AND content_generation>0 AND target_revision>0 AND owner_type IN('song','singer','album','song_artifact') AND cloud_provider IN('baidu','gdrive'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
