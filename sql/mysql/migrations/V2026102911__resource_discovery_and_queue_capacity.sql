-- 持久发现只保存受控元数据；队列上限由数据库统一约束所有实例。
CREATE TABLE IF NOT EXISTS resource_discovery_runs (
 run_id VARCHAR(96) CHARACTER SET ascii COLLATE ascii_bin NOT NULL PRIMARY KEY,
 definition_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 definition JSON NOT NULL,
 owner_type VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 destination VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 high_water_id BIGINT UNSIGNED NOT NULL,
 cursor_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
 state VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 claim_generation BIGINT UNSIGNED NOT NULL DEFAULT 0,
 lease_until BIGINT UNSIGNED NOT NULL DEFAULT 0,
 processed BIGINT UNSIGNED NOT NULL DEFAULT 0,
 counts JSON NOT NULL,
 result_code VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
 active_destination VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin GENERATED ALWAYS AS (CASE WHEN state='running' THEN destination ELSE NULL END) STORED,
 created_at BIGINT UNSIGNED NOT NULL,
 updated_at BIGINT UNSIGNED NOT NULL,
 UNIQUE KEY uk_discovery_active_destination(active_destination),
 KEY idx_discovery_state(state,destination,updated_at,run_id),
 CONSTRAINT ck_discovery_scope CHECK(owner_type IN('song','singer','album','song_artifact') AND destination IN('baidu','gdrive')),
 CONSTRAINT ck_discovery_state CHECK(state IN('running','paused','completed','cancelled') AND cursor_id<=high_water_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS resource_discovery_observations (
 run_id VARCHAR(96) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 resource_id BIGINT UNSIGNED NOT NULL,
 content_generation BIGINT UNSIGNED NOT NULL,
 source_identity CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 values_digest CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 grade VARCHAR(24) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 observation JSON NOT NULL,
 observed_at BIGINT UNSIGNED NOT NULL,
 PRIMARY KEY(run_id,resource_id),
 KEY idx_discovery_grade(run_id,grade,resource_id),
 CONSTRAINT fk_discovery_observation_run FOREIGN KEY(run_id) REFERENCES resource_discovery_runs(run_id) ON DELETE CASCADE,
 CONSTRAINT ck_discovery_observation CHECK(resource_id>0 AND content_generation>0 AND grade IN('observed','hash_matched','conflict','missing','unknown','unresolved','source_changed'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

DROP PROCEDURE IF EXISTS crawler_resource_queue_expand;
DELIMITER $$
CREATE PROCEDURE crawler_resource_queue_expand()
BEGIN
 IF NOT EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='resource_destination_states' AND column_name='max_queued_transfers') THEN
  ALTER TABLE resource_destination_states ADD COLUMN max_queued_transfers BIGINT UNSIGNED NOT NULL DEFAULT 10000;
 END IF;
 IF NOT EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='resource_destination_states' AND column_name='max_queued_producers') THEN
  ALTER TABLE resource_destination_states ADD COLUMN max_queued_producers BIGINT UNSIGNED NOT NULL DEFAULT 10000;
 END IF;
 IF NOT EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='resource_destination_states' AND column_name='queue_limits_version') THEN
  ALTER TABLE resource_destination_states ADD COLUMN queue_limits_version BIGINT UNSIGNED NOT NULL DEFAULT 1;
 END IF;
 IF NOT EXISTS(SELECT 1 FROM information_schema.table_constraints WHERE constraint_schema=DATABASE() AND table_name='resource_destination_states' AND constraint_name='ck_resource_queue_limits') THEN
  ALTER TABLE resource_destination_states ADD CONSTRAINT ck_resource_queue_limits CHECK(max_queued_transfers BETWEEN 1 AND 1000000 AND max_queued_producers BETWEEN 1 AND 1000000 AND queue_limits_version>0);
 END IF;
END$$
CALL crawler_resource_queue_expand()$$
DROP PROCEDURE crawler_resource_queue_expand$$
DELIMITER ;
