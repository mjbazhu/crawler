-- 合法音频响应只记录关联种类；不保存临时URL或下载正文。
CREATE TABLE IF NOT EXISTS resource_transfer_related (
    parent_task_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    discovered_mask TINYINT UNSIGNED NOT NULL,
    claim_generation BIGINT UNSIGNED NOT NULL,
    child_operation_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin DEFAULT NULL,
    created_at BIGINT UNSIGNED NOT NULL,
    updated_at BIGINT UNSIGNED NOT NULL,
    CONSTRAINT fk_related_parent_task FOREIGN KEY(parent_task_id) REFERENCES resource_transfer_tasks(task_id) ON DELETE CASCADE,
    CONSTRAINT ck_related_mask CHECK(discovered_mask<=15)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 受控内部父关系可按索引查两层后代，避免JSON条件全表扫描。
ALTER TABLE resource_transfer_operations
 ADD COLUMN parent_operation_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin GENERATED ALWAYS AS (NULLIF(JSON_UNQUOTE(JSON_EXTRACT(request_snapshot,'$.parent_operation_id')),'')) STORED,
 ADD COLUMN parent_task_id BIGINT UNSIGNED GENERATED ALWAYS AS (CAST(JSON_UNQUOTE(JSON_EXTRACT(request_snapshot,'$.parent_task_id')) AS UNSIGNED)) STORED,
 ADD KEY idx_transfer_parent_operation(parent_operation_id,operation_id);
