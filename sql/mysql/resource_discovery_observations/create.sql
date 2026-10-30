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
