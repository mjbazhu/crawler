-- 持久三路任务扩展：不激活目标，不读取或传输任何正文。
SET @transfer_jobs_ddl := CONCAT_WS(',',
 IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resource_artifacts' AND column_name='claim_generation'),NULL,'ADD COLUMN claim_generation BIGINT UNSIGNED NOT NULL DEFAULT 0'),
 IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resource_artifacts' AND column_name='running_batch_id'),NULL,'ADD COLUMN running_batch_id VARCHAR(128) NOT NULL DEFAULT '''''),
 IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resource_artifacts' AND column_name='claimed_at'),NULL,'ADD COLUMN claimed_at BIGINT UNSIGNED NOT NULL DEFAULT 0'));
SET @transfer_jobs_sql := IF(@transfer_jobs_ddl='','SELECT 1',CONCAT('ALTER TABLE song_resource_artifacts ',@transfer_jobs_ddl));
PREPARE transfer_jobs_stmt FROM @transfer_jobs_sql;
EXECUTE transfer_jobs_stmt;
DEALLOCATE PREPARE transfer_jobs_stmt;
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
    PRIMARY KEY (operation_id),
    UNIQUE KEY uk_transfer_idempotency (idempotency_key),
    KEY idx_transfer_operation_state (state,updated_at,operation_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 任务历史保留资源身份而不随资源删除；执行时必须重查当前资源和版本。
CREATE TABLE IF NOT EXISTS resource_transfer_tasks (
    task_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    operation_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    owner_type VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    resource_id BIGINT UNSIGNED NOT NULL,
    destination VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    content_generation BIGINT UNSIGNED NOT NULL,
    source_revision BIGINT UNSIGNED NOT NULL,
    source_identity CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    target_revision BIGINT UNSIGNED NOT NULL,
    target_fingerprint CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    state VARCHAR(24) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT 'pending',
    claim_generation BIGINT UNSIGNED NOT NULL DEFAULT 0,
    native_claim_generation BIGINT UNSIGNED NOT NULL DEFAULT 0,
    claimed_at BIGINT UNSIGNED NOT NULL DEFAULT 0,
    available_at BIGINT UNSIGNED NOT NULL DEFAULT 0,
    attempt_count INT UNSIGNED NOT NULL DEFAULT 0,
    timeout_ms INT UNSIGNED NOT NULL,
    retries INT UNSIGNED NOT NULL,
    max_bytes BIGINT UNSIGNED NOT NULL,
    bytes_per_second BIGINT UNSIGNED NOT NULL DEFAULT 0,
    cancel_requested TINYINT UNSIGNED NOT NULL DEFAULT 0,
    busy_task_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    attempt_path TEXT DEFAULT NULL,
    attempt_checksum CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
    attempt_size_bytes BIGINT UNSIGNED NOT NULL DEFAULT 0,
    result_code VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
    error_category VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
    created_at BIGINT UNSIGNED NOT NULL,
    updated_at BIGINT UNSIGNED NOT NULL,
    PRIMARY KEY (task_id),
    UNIQUE KEY uk_transfer_operation_item (operation_id,owner_type,resource_id,destination),
    KEY idx_transfer_dispatch (destination,state,available_at,task_id),
    CONSTRAINT fk_transfer_task_operation FOREIGN KEY (operation_id) REFERENCES resource_transfer_operations(operation_id) ON DELETE CASCADE,
    CONSTRAINT ck_transfer_task_owner CHECK (owner_type IN ('song','singer','album','song_artifact') AND resource_id>0),
    CONSTRAINT ck_transfer_task_destination CHECK (destination IN ('local','baidu','gdrive'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 同一资源目标一次只属于一个操作；跨操作冲突返回 busy，不接管取消权。
CREATE TABLE IF NOT EXISTS resource_transfer_slots (
    owner_type VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    resource_id BIGINT UNSIGNED NOT NULL,
    destination VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    task_id BIGINT UNSIGNED NOT NULL,
    PRIMARY KEY (owner_type,resource_id,destination),
    UNIQUE KEY uk_transfer_slot_task (task_id),
    CONSTRAINT fk_transfer_slot_task FOREIGN KEY (task_id) REFERENCES resource_transfer_tasks(task_id) ON DELETE CASCADE,
    CONSTRAINT ck_transfer_slot_owner CHECK (owner_type IN ('song','singer','album','song_artifact') AND resource_id>0),
    CONSTRAINT ck_transfer_slot_destination CHECK (destination IN ('local','baidu','gdrive'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

