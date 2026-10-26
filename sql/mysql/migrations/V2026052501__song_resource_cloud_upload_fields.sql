SET @column_exists := (
    SELECT COUNT(*)
    FROM information_schema.columns
    WHERE table_schema = DATABASE()
      AND table_name = 'song_resources'
      AND column_name = 'cloud_backend'
);
SET @ddl := IF(
    @column_exists = 0,
    'ALTER TABLE song_resources ADD COLUMN cloud_backend VARCHAR(32) NOT NULL DEFAULT '''' COMMENT ''云端上传后端，例如 baidupcs；未上传时为空'' AFTER local_path',
    'SELECT 1'
);
PREPARE stmt FROM @ddl;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @column_exists := (
    SELECT COUNT(*)
    FROM information_schema.columns
    WHERE table_schema = DATABASE()
      AND table_name = 'song_resources'
      AND column_name = 'cloud_path'
);
SET @ddl := IF(
    @column_exists = 0,
    'ALTER TABLE song_resources ADD COLUMN cloud_path TEXT COMMENT ''资源上传到云端网盘后的稳定路径'' AFTER cloud_backend',
    'SELECT 1'
);
PREPARE stmt FROM @ddl;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @column_exists := (
    SELECT COUNT(*)
    FROM information_schema.columns
    WHERE table_schema = DATABASE()
      AND table_name = 'song_resources'
      AND column_name = 'cloud_uploaded_at'
);
SET @ddl := IF(
    @column_exists = 0,
    'ALTER TABLE song_resources ADD COLUMN cloud_uploaded_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''资源最近一次成功上传到云端的时间，Unix 秒时间戳'' AFTER fetched_at',
    'SELECT 1'
);
PREPARE stmt FROM @ddl;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
