-- 三个目标各保存一份当前配置与维护闸门；不保存认证。
CREATE TABLE IF NOT EXISTS resource_destination_states (
 max_queued_transfers BIGINT UNSIGNED NOT NULL DEFAULT 10000,
 max_queued_producers BIGINT UNSIGNED NOT NULL DEFAULT 10000,
 queue_limits_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
 CONSTRAINT ck_resource_queue_limits CHECK(max_queued_transfers BETWEEN 1 AND 1000000 AND max_queued_producers BETWEEN 1 AND 1000000 AND queue_limits_version>0),

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
