-- singers 保留名称池去重；真实歌手由 source/source_singer_id 区分，类型归来源记录。
SET @singer_gender_ddl := IF(
    NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='singer_source_refs' AND column_name='source_gender'),
    'ALTER TABLE singer_source_refs ADD COLUMN source_gender INT NOT NULL DEFAULT 0 COMMENT ''来源歌手类型：0 未知，1 男，2 女，3 组合；按平台歌手 ID 隔离'' AFTER source_singer_id',
    'SELECT 1'
);
PREPARE singer_gender_statement FROM @singer_gender_ddl;
EXECUTE singer_gender_statement;
DEALLOCATE PREPARE singer_gender_statement;

-- 优先用来源自己的索引分组恢复。共享名称的旧类型不可信；无法唯一恢复时保持未知。
UPDATE singer_source_refs r
JOIN singers s ON s.id=r.singer_db_id
LEFT JOIN (
    SELECT singer_source_ref_id, COUNT(DISTINCT index_gender) AS gender_count, MIN(index_gender) AS index_gender
    FROM (
        SELECT singer_source_ref_id,
               CASE SUBSTRING_INDEX(tab,'-',-1) WHEN 'nan' THEN 1 WHEN 'nv' THEN 2 WHEN 'group' THEN 3 END AS index_gender
        FROM singer_index_memberships
    ) membership_types
    WHERE index_gender IS NOT NULL
    GROUP BY singer_source_ref_id
) classification ON classification.singer_source_ref_id=r.id
LEFT JOIN (
    SELECT singer_db_id, COUNT(*) AS source_count
    FROM singer_source_refs
    GROUP BY singer_db_id
) owners ON owners.singer_db_id=r.singer_db_id
SET r.source_gender=CASE
    WHEN classification.gender_count=1 THEN classification.index_gender
    WHEN classification.gender_count>1 THEN 0
    WHEN owners.source_count=1 AND s.gender BETWEEN 1 AND 3 THEN s.gender
    ELSE 0
END
WHERE r.source_gender=0;

-- 保留旧公共仓储的列兼容，但名称池不再承担来源类型。
UPDATE singers SET gender=0 WHERE gender<>0;
ALTER TABLE singers MODIFY COLUMN gender INT NOT NULL DEFAULT 0 COMMENT '兼容字段固定为0；来源歌手类型见 singer_source_refs.source_gender';

SET @singer_gender_ddl := IF(
    NOT EXISTS (SELECT 1 FROM information_schema.table_constraints WHERE constraint_schema=DATABASE() AND table_name='singer_source_refs' AND constraint_name='chk_singer_source_gender'),
    'ALTER TABLE singer_source_refs ADD CONSTRAINT chk_singer_source_gender CHECK (source_gender IN (0,1,2,3))',
    'SELECT 1'
);
PREPARE singer_gender_statement FROM @singer_gender_ddl;
EXECUTE singer_gender_statement;
DEALLOCATE PREPARE singer_gender_statement;

-- 重建 SELECT r.* 的兼容视图，使新增来源字段对旧查询入口可见。
CREATE OR REPLACE VIEW singer_catalog_sources AS
SELECT r.*,CONCAT('singer:',r.source_singer_id) AS source_item_key,
       COALESCE((SELECT m.source_tag FROM singer_index_memberships m WHERE m.singer_source_ref_id=r.id AND NULLIF(m.source_tag,'') IS NOT NULL ORDER BY m.id LIMIT 1),'') AS source_tag
FROM singer_source_refs r;

INSERT INTO crawler_schema_versions(component,schema_version,schema_name,applied_at,updated_at)
VALUES('crawler_mysql',2026102702,'singer source identity and gender ownership',UNIX_TIMESTAMP(),UNIX_TIMESTAMP())
ON DUPLICATE KEY UPDATE schema_version=GREATEST(schema_version,VALUES(schema_version)),schema_name=VALUES(schema_name),updated_at=VALUES(updated_at);
