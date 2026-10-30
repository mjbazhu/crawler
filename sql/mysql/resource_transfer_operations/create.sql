-- 真实三路操作；幂等键绑定首次请求，冻结配置不保存凭据或临时 URL。
CREATE TABLE IF NOT EXISTS resource_transfer_operations (
    operation_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    idempotency_key VARBINARY(128) DEFAULT NULL,
    request_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    request_snapshot JSON NOT NULL,
    action VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    state VARCHAR(24) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT 'running',
    logical_resource_count INT UNSIGNED NOT NULL,
    task_count INT UNSIGNED NOT NULL,
    created_at BIGINT UNSIGNED NOT NULL,
    updated_at BIGINT UNSIGNED NOT NULL,
    parent_operation_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin GENERATED ALWAYS AS (NULLIF(JSON_UNQUOTE(JSON_EXTRACT(request_snapshot,'$.parent_operation_id')),'')) STORED,
    parent_task_id BIGINT UNSIGNED GENERATED ALWAYS AS (CAST(JSON_UNQUOTE(JSON_EXTRACT(request_snapshot,'$.parent_task_id')) AS UNSIGNED)) STORED,
    PRIMARY KEY (operation_id),
    KEY idx_transfer_parent_operation(parent_operation_id,operation_id),
    UNIQUE KEY uk_transfer_idempotency (idempotency_key),
    KEY idx_transfer_operation_state (state,updated_at,operation_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
