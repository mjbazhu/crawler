-- 常用目录先按来源和更新时间分页，再为本页读取关系与资源统计。
SET @catalog_ddl := IF(EXISTS(SELECT 1 FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='song_source_refs' AND index_name='idx_song_source_recent'), 'SELECT 1', 'ALTER TABLE song_source_refs ADD KEY idx_song_source_recent(source,updated_at,id)');
PREPARE catalog_stmt FROM @catalog_ddl;
EXECUTE catalog_stmt;
DEALLOCATE PREPARE catalog_stmt;

SET @catalog_ddl := IF(EXISTS(SELECT 1 FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='album_source_refs' AND index_name='idx_album_source_recent'), 'SELECT 1', 'ALTER TABLE album_source_refs ADD KEY idx_album_source_recent(source,updated_at,id)');
PREPARE catalog_stmt FROM @catalog_ddl;
EXECUTE catalog_stmt;
DEALLOCATE PREPARE catalog_stmt;

-- 历史列表只有已识别的专辑类型时，把详情请求类型规范到唯一保留的字段。
UPDATE album_source_refs
SET source_resource_type=CASE WHEN source_album_kind=2 THEN '5' ELSE '2003' END
WHERE NULLIF(source_resource_type,'') IS NULL AND source_album_kind IN(1,2);

INSERT INTO crawler_schema_versions(component,schema_version,schema_name,applied_at,updated_at)
VALUES('crawler_mysql',2026102502,'normalized catalog pagination',UNIX_TIMESTAMP(),UNIX_TIMESTAMP())
ON DUPLICATE KEY UPDATE schema_version=GREATEST(schema_version,VALUES(schema_version)),schema_name=VALUES(schema_name),updated_at=VALUES(updated_at);
