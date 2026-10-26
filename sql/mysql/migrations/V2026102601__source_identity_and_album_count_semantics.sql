-- 来源身份保留真实平台 ID；歌曲至少有一个有效 ID，约束先于删列执行。
SET @source_cleanup_ddl := IF(EXISTS(SELECT 1 FROM information_schema.table_constraints WHERE constraint_schema=DATABASE() AND table_name='song_source_refs' AND constraint_name='chk_song_source_identity'), 'SELECT 1', 'ALTER TABLE song_source_refs ADD CONSTRAINT chk_song_source_identity CHECK (NULLIF(TRIM(source_song_id),'''') IS NOT NULL OR NULLIF(TRIM(source_content_id),'''') IS NOT NULL)');
PREPARE source_cleanup_stmt FROM @source_cleanup_ddl;
EXECUTE source_cleanup_stmt;
DEALLOCATE PREPARE source_cleanup_stmt;

-- 标题由公共专辑表维护，仅在公共标题为空时承接历史来源标题。
SET @source_cleanup_ddl := IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_source_refs' AND column_name='source_title'), 'UPDATE albums a JOIN album_source_refs r ON r.album_db_id=a.id SET a.title=r.source_title,a.normalized_title=r.source_title WHERE a.title='''' AND NULLIF(r.source_title,'''') IS NOT NULL', 'SELECT 1');
PREPARE source_cleanup_stmt FROM @source_cleanup_ddl;
EXECUTE source_cleanup_stmt;
DEALLOCATE PREPARE source_cleanup_stmt;

DROP VIEW IF EXISTS song_catalog_sources;
DROP VIEW IF EXISTS singer_catalog_sources;
DROP VIEW IF EXISTS album_catalog_sources;

-- 删除派生列前移除整组旧唯一索引，避免索引退化成 source 单列唯一。
SET @source_cleanup_ddl := IF(EXISTS(SELECT 1 FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='song_source_refs' AND index_name='uk_source_item'), 'ALTER TABLE song_source_refs DROP INDEX uk_source_item', 'SELECT 1');
PREPARE source_cleanup_stmt FROM @source_cleanup_ddl;
EXECUTE source_cleanup_stmt;
DEALLOCATE PREPARE source_cleanup_stmt;

SET @source_cleanup_ddl := IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_source_refs' AND column_name='source_item_key'), 'ALTER TABLE song_source_refs DROP COLUMN source_item_key', 'SELECT 1');
PREPARE source_cleanup_stmt FROM @source_cleanup_ddl;
EXECUTE source_cleanup_stmt;
DEALLOCATE PREPARE source_cleanup_stmt;

-- 删除派生列前移除整组旧唯一索引，避免索引退化成 source 单列唯一。
SET @source_cleanup_ddl := IF(EXISTS(SELECT 1 FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='singer_source_refs' AND index_name='uk_singer_source_item'), 'ALTER TABLE singer_source_refs DROP INDEX uk_singer_source_item', 'SELECT 1');
PREPARE source_cleanup_stmt FROM @source_cleanup_ddl;
EXECUTE source_cleanup_stmt;
DEALLOCATE PREPARE source_cleanup_stmt;

SET @source_cleanup_ddl := IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='singer_source_refs' AND column_name='source_item_key'), 'ALTER TABLE singer_source_refs DROP COLUMN source_item_key', 'SELECT 1');
PREPARE source_cleanup_stmt FROM @source_cleanup_ddl;
EXECUTE source_cleanup_stmt;
DEALLOCATE PREPARE source_cleanup_stmt;

-- 删除派生列前移除整组旧唯一索引，避免索引退化成 source 单列唯一。
SET @source_cleanup_ddl := IF(EXISTS(SELECT 1 FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='album_source_refs' AND index_name='uk_album_source_item'), 'ALTER TABLE album_source_refs DROP INDEX uk_album_source_item', 'SELECT 1');
PREPARE source_cleanup_stmt FROM @source_cleanup_ddl;
EXECUTE source_cleanup_stmt;
DEALLOCATE PREPARE source_cleanup_stmt;

SET @source_cleanup_ddl := IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_source_refs' AND column_name='source_item_key'), 'ALTER TABLE album_source_refs DROP COLUMN source_item_key', 'SELECT 1');
PREPARE source_cleanup_stmt FROM @source_cleanup_ddl;
EXECUTE source_cleanup_stmt;
DEALLOCATE PREPARE source_cleanup_stmt;

SET @source_cleanup_ddl := IF(EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_source_refs' AND column_name='source_title'), 'ALTER TABLE album_source_refs DROP COLUMN source_title', 'SELECT 1');
PREPARE source_cleanup_stmt FROM @source_cleanup_ddl;
EXECUTE source_cleanup_stmt;
DEALLOCATE PREPARE source_cleanup_stmt;

ALTER TABLE albums MODIFY COLUMN identity_hash CHAR(64) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '专辑去重键 SHA-256，来源于 source 与 source_album_id，以竖线分隔';

CREATE OR REPLACE VIEW singer_catalog_sources AS
SELECT r.*,CONCAT('singer:',r.source_singer_id) AS source_item_key,
       COALESCE((SELECT m.source_tag FROM singer_index_memberships m WHERE m.singer_source_ref_id=r.id AND NULLIF(m.source_tag,'') IS NOT NULL ORDER BY m.id LIMIT 1),'') AS source_tag
FROM singer_source_refs r;

CREATE OR REPLACE VIEW album_catalog_sources AS
SELECT r.*,CONCAT('album:',r.source_album_id) AS source_item_key,a.title AS source_title,
       COALESCE((SELECT s.source_singer_id FROM album_artist_refs ar JOIN singer_source_refs s ON s.id=ar.singer_source_ref_id WHERE ar.album_source_ref_id=r.id ORDER BY ar.is_primary DESC,ar.is_seed DESC,ar.artist_order,ar.id LIMIT 1),'') AS source_artist_id,
       COALESCE((SELECT s.name FROM album_artist_refs ar JOIN singer_source_refs sr ON sr.id=ar.singer_source_ref_id JOIN singers s ON s.id=sr.singer_db_id WHERE ar.album_source_ref_id=r.id ORDER BY ar.is_primary DESC,ar.is_seed DESC,ar.artist_order,ar.id LIMIT 1),'') AS source_artist_name,
       CASE WHEN r.source_resource_type='5' THEN '1' ELSE '0' END AS source_res_type,
       COALESCE(CAST(r.source_song_total AS CHAR),'') AS total_count,
       a.release_date AS release_date,a.release_date AS publish_date,0 AS play_num,'' AS play_num_desc
FROM album_source_refs r JOIN albums a ON a.id=r.album_db_id;

CREATE OR REPLACE VIEW song_catalog_sources AS
SELECT r.*,CASE WHEN NULLIF(TRIM(r.source_content_id),'') IS NOT NULL THEN CONCAT('content:',r.source_content_id) ELSE CONCAT('song:',r.source_song_id) END AS source_item_key,
       COALESCE((SELECT a.source_album_id FROM album_song_refs ar JOIN album_source_refs a ON a.id=ar.album_source_ref_id WHERE ar.song_source_ref_id=r.id ORDER BY ar.id LIMIT 1),'') AS source_album_id,
       COALESCE((SELECT title_row.title FROM album_song_refs ar JOIN album_source_refs a ON a.id=ar.album_source_ref_id JOIN albums title_row ON title_row.id=a.album_db_id WHERE ar.song_source_ref_id=r.id ORDER BY ar.id LIMIT 1),'') AS source_album_name
FROM song_source_refs r;

-- 同步状态及最近一次处理条数原样保留，已有关系数量由查询单独统计。
INSERT INTO crawler_schema_versions(component,schema_version,schema_name,applied_at,updated_at)
VALUES('crawler_mysql',2026102601,'source identity cleanup and album count semantics',UNIX_TIMESTAMP(),UNIX_TIMESTAMP())
ON DUPLICATE KEY UPDATE schema_version=GREATEST(schema_version,VALUES(schema_version)),schema_name=VALUES(schema_name),updated_at=VALUES(updated_at);
