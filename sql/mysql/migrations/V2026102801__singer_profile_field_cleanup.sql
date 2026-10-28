-- 公共名称池只保留名称；来源分类和目录生命周期继续独立维护。
DROP VIEW IF EXISTS singer_catalog_sources;

SET @singer_profile_cleanup_ddl := IF(
    EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='singers' AND column_name='gender'),
    'ALTER TABLE singers DROP COLUMN gender',
    'SELECT 1'
);
PREPARE singer_profile_cleanup_statement FROM @singer_profile_cleanup_ddl;
EXECUTE singer_profile_cleanup_statement;
DEALLOCATE PREPARE singer_profile_cleanup_statement;

-- 删除 status 同时删除仅包含该列的 idx_singers_status，不改动名称唯一键。
SET @singer_profile_cleanup_ddl := IF(
    EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='singers' AND column_name='status'),
    'ALTER TABLE singers DROP COLUMN status',
    'SELECT 1'
);
PREPARE singer_profile_cleanup_statement FROM @singer_profile_cleanup_ddl;
EXECUTE singer_profile_cleanup_statement;
DEALLOCATE PREPARE singer_profile_cleanup_statement;

SET @singer_profile_cleanup_ddl := IF(
    EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='singer_source_refs' AND column_name='metadata_status'),
    'ALTER TABLE singer_source_refs DROP COLUMN metadata_status',
    'SELECT 1'
);
PREPARE singer_profile_cleanup_statement FROM @singer_profile_cleanup_ddl;
EXECUTE singer_profile_cleanup_statement;
DEALLOCATE PREPARE singer_profile_cleanup_statement;

CREATE OR REPLACE VIEW singer_catalog_sources AS
SELECT r.*,CONCAT('singer:',r.source_singer_id) AS source_item_key,
       COALESCE((SELECT m.source_tag FROM singer_index_memberships m WHERE m.singer_source_ref_id=r.id AND NULLIF(m.source_tag,'') IS NOT NULL ORDER BY m.id LIMIT 1),'') AS source_tag
FROM singer_source_refs r;

INSERT INTO crawler_schema_versions(component,schema_version,schema_name,applied_at,updated_at)
VALUES('crawler_mysql',2026102801,'singer profile field cleanup',UNIX_TIMESTAMP(),UNIX_TIMESTAMP())
ON DUPLICATE KEY UPDATE schema_version=GREATEST(schema_version,VALUES(schema_version)),schema_name=VALUES(schema_name),updated_at=VALUES(updated_at);
