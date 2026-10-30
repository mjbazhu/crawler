-- 基线封存保留完整审阅摘要；不靠 active 单字段表示迁移已完成。
CREATE TABLE IF NOT EXISTS resource_cloud_baselines (
 baseline_id VARCHAR(96) CHARACTER SET ascii COLLATE ascii_bin NOT NULL PRIMARY KEY,
 manifest_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 plan JSON NOT NULL,
 state VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 created_at BIGINT UNSIGNED NOT NULL,
 activated_at BIGINT UNSIGNED DEFAULT NULL,
 CONSTRAINT ck_resource_baseline_state CHECK (state IN ('sealed','active'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
