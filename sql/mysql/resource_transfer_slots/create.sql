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
