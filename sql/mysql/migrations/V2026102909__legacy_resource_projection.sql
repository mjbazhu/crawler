-- 旧单云视图首次显式启用后固定；关闭执行目标不改变历史读取归属。
CREATE TABLE IF NOT EXISTS resource_legacy_profiles (
 profile_key VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 cloud_provider VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 created_at BIGINT UNSIGNED NOT NULL,
 PRIMARY KEY(profile_key),
 CONSTRAINT ck_legacy_profile_provider CHECK(cloud_provider IN ('local','baidu','gdrive'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 旧批次只展示固定完成范围，真实操作不重复进入 crawl_batches。
CREATE TABLE IF NOT EXISTS resource_legacy_batches (
 id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
 batch_id VARCHAR(128) COLLATE utf8mb4_unicode_ci NOT NULL,
 owner_type VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 cloud_provider VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 concurrency_limit INT UNSIGNED NOT NULL,
 request_snapshot JSON NOT NULL,
 terminal_result JSON DEFAULT NULL,
 created_at BIGINT UNSIGNED NOT NULL,
 finished_at BIGINT UNSIGNED NOT NULL DEFAULT 0,
 PRIMARY KEY(id), UNIQUE KEY uk_legacy_batch_id(batch_id),
 KEY idx_legacy_batch_active(finished_at,id),
 CONSTRAINT fk_legacy_crawl_batch FOREIGN KEY(batch_id) REFERENCES crawl_batches(batch_id) ON DELETE CASCADE,
 CONSTRAINT ck_legacy_batch_owner CHECK(owner_type IN ('song','singer','album')),
 CONSTRAINT ck_legacy_batch_provider CHECK(cloud_provider IN ('local','baidu','gdrive')),
 CONSTRAINT ck_legacy_batch_concurrency CHECK(concurrency_limit BETWEEN 1 AND 32)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 一次旧请求最多1000逻辑资源，以200项真实操作分块，整个登记事务原子提交。
CREATE TABLE IF NOT EXISTS resource_legacy_operations (
 batch_id VARCHAR(128) COLLATE utf8mb4_unicode_ci NOT NULL,
 operation_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 chunk_index INT UNSIGNED NOT NULL,
 PRIMARY KEY(batch_id,chunk_index), UNIQUE KEY uk_legacy_operation(operation_id),
 CONSTRAINT fk_legacy_operation_batch FOREIGN KEY(batch_id) REFERENCES resource_legacy_batches(batch_id) ON DELETE CASCADE,
 CONSTRAINT fk_legacy_operation_transfer FOREIGN KEY(operation_id) REFERENCES resource_transfer_operations(operation_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
