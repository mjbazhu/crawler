-- 一次旧请求最多1000逻辑资源，以200项真实操作分块，整个登记事务原子提交。
CREATE TABLE IF NOT EXISTS resource_legacy_operations (
 batch_id VARCHAR(128) COLLATE utf8mb4_unicode_ci NOT NULL,
 operation_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 chunk_index INT UNSIGNED NOT NULL,
 PRIMARY KEY(batch_id,chunk_index), UNIQUE KEY uk_legacy_operation(operation_id),
 CONSTRAINT fk_legacy_operation_batch FOREIGN KEY(batch_id) REFERENCES resource_legacy_batches(batch_id) ON DELETE CASCADE,
 CONSTRAINT fk_legacy_operation_transfer FOREIGN KEY(operation_id) REFERENCES resource_transfer_operations(operation_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
