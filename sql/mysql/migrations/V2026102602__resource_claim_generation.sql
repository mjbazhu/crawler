-- 每次认领递增，防止恢复、重置或重试后旧 worker 回写覆盖新任务。
SET @resource_claim_ddl := IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resources' AND column_name='claim_generation'), 'SELECT 1', 'ALTER TABLE song_resources ADD COLUMN claim_generation BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''资源认领代次，成功与失败回写必须匹配'' AFTER running_batch_id');
PREPARE resource_claim_stmt FROM @resource_claim_ddl;
EXECUTE resource_claim_stmt;
DEALLOCATE PREPARE resource_claim_stmt;
