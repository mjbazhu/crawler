-- 旧单云视图首次显式启用后固定；关闭执行目标不改变历史读取归属。
CREATE TABLE IF NOT EXISTS resource_legacy_profiles (
 profile_key VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 cloud_provider VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 created_at BIGINT UNSIGNED NOT NULL,
 PRIMARY KEY(profile_key),
 CONSTRAINT ck_legacy_profile_provider CHECK(cloud_provider IN ('local','baidu','gdrive'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
