-- 歌手和专辑图片地址随元数据保存，下载由后续独立资源流程处理。
SET @metadata_cover_url_ddl := IF(
    NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='singer_resources' AND column_name='remote_url'),
    'ALTER TABLE singer_resources ADD COLUMN remote_url TEXT COLLATE utf8mb4_unicode_ci NULL COMMENT ''采集响应中的歌手头像地址，供后续独立下载'' AFTER resource_kind',
    'SELECT 1'
);
PREPARE metadata_cover_url_statement FROM @metadata_cover_url_ddl;
EXECUTE metadata_cover_url_statement;
DEALLOCATE PREPARE metadata_cover_url_statement;

SET @metadata_cover_url_ddl := IF(
    NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_resources' AND column_name='remote_url'),
    'ALTER TABLE album_resources ADD COLUMN remote_url TEXT COLLATE utf8mb4_unicode_ci NULL COMMENT ''采集响应中的专辑封面地址，供后续独立下载'' AFTER resource_kind',
    'SELECT 1'
);
PREPARE metadata_cover_url_statement FROM @metadata_cover_url_ddl;
EXECUTE metadata_cover_url_statement;
DEALLOCATE PREPARE metadata_cover_url_statement;

ALTER TABLE singer_resources COMMENT='歌手封面地址与本地保存结果，采集阶段只登记地址';
ALTER TABLE album_resources COMMENT='专辑封面地址与本地保存结果，采集阶段只登记地址';

INSERT INTO crawler_schema_versions(component,schema_version,schema_name,applied_at,updated_at)
VALUES('crawler_mysql',2026102703,'metadata cover remote URLs without inline downloads',UNIX_TIMESTAMP(),UNIX_TIMESTAMP())
ON DUPLICATE KEY UPDATE schema_version=GREATEST(schema_version,VALUES(schema_version)),schema_name=VALUES(schema_name),updated_at=VALUES(updated_at);
