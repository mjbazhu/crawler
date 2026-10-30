-- 未被历史保存声明覆盖的资源保持 blocked；明确刷新内容代次才解除这一代的抑制。
CREATE TABLE IF NOT EXISTS resource_baseline_blocks (
 owner_type VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 resource_id BIGINT UNSIGNED NOT NULL,
 content_generation BIGINT UNSIGNED NOT NULL,
 baseline_id VARCHAR(96) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 reason VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT 'historical_scope_excluded',
 PRIMARY KEY (owner_type,resource_id,content_generation),
 CONSTRAINT ck_resource_baseline_owner CHECK (owner_type IN ('song','singer','album','song_artifact'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
