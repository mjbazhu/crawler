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
