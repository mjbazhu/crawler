-- 按实体、关系和资源职责收敛结构；保留已有资源文件结果，不修改历史 migration。

CREATE TABLE IF NOT EXISTS album_artist_refs (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '专辑歌手关系主键',
    album_source_ref_id BIGINT UNSIGNED NOT NULL COMMENT '平台专辑来源主键',
    singer_source_ref_id BIGINT UNSIGNED NOT NULL COMMENT '平台歌手来源主键',
    artist_role VARCHAR(64) NOT NULL DEFAULT 'artist' COMMENT '歌手角色',
    artist_order INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '平台返回顺序，0 表示未知',
    is_primary TINYINT NOT NULL DEFAULT 0 COMMENT '是否主歌手',
    is_seed TINYINT NOT NULL DEFAULT 0 COMMENT '是否采集入口歌手',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '创建时间',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '更新时间',
    PRIMARY KEY(id),
    UNIQUE KEY uk_album_artist (album_source_ref_id, singer_source_ref_id, artist_role),
    KEY idx_album_artist_singer (singer_source_ref_id, album_source_ref_id),
    CONSTRAINT fk_album_artist_album FOREIGN KEY(album_source_ref_id) REFERENCES album_source_refs(id) ON DELETE CASCADE,
    CONSTRAINT fk_album_artist_singer FOREIGN KEY(singer_source_ref_id) REFERENCES singer_source_refs(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='专辑与歌手关系，不保存歌手属性';

CREATE TABLE IF NOT EXISTS singer_resources (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '封面资源主键',
    singer_source_ref_id BIGINT UNSIGNED NOT NULL COMMENT '资源所属来源主键',
    resource_kind VARCHAR(16) NOT NULL DEFAULT 'cover' COMMENT '资源类型，当前为封面',
    local_path TEXT COMMENT '模块内相对存储路径',
    size_bytes BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '实际保存字节数',
    checksum VARCHAR(128) COMMENT '完整文件校验值',
    status TINYINT NOT NULL DEFAULT 0 COMMENT '0 待获取，2 已完成，3 失败',
    error_category VARCHAR(64) NOT NULL DEFAULT '' COMMENT '失败分类',
    result_code VARCHAR(64) NOT NULL DEFAULT '' COMMENT '处理结果码',
    last_error TEXT COMMENT '脱敏失败原因',
    fetched_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '成功保存时间',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '创建时间',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '更新时间',
    PRIMARY KEY (id),
    UNIQUE KEY uk_singer_resources_owner (singer_source_ref_id, resource_kind),
    CONSTRAINT fk_singer_resources_owner FOREIGN KEY (singer_source_ref_id) REFERENCES singer_source_refs(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='本地封面资源，不持久化远端地址';

CREATE TABLE IF NOT EXISTS album_resources (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '封面资源主键',
    album_source_ref_id BIGINT UNSIGNED NOT NULL COMMENT '资源所属来源主键',
    resource_kind VARCHAR(16) NOT NULL DEFAULT 'cover' COMMENT '资源类型，当前为封面',
    local_path TEXT COMMENT '模块内相对存储路径',
    size_bytes BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '实际保存字节数',
    checksum VARCHAR(128) COMMENT '完整文件校验值',
    status TINYINT NOT NULL DEFAULT 0 COMMENT '0 待获取，2 已完成，3 失败',
    error_category VARCHAR(64) NOT NULL DEFAULT '' COMMENT '失败分类',
    result_code VARCHAR(64) NOT NULL DEFAULT '' COMMENT '处理结果码',
    last_error TEXT COMMENT '脱敏失败原因',
    fetched_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '成功保存时间',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '创建时间',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '更新时间',
    PRIMARY KEY (id),
    UNIQUE KEY uk_album_resources_owner (album_source_ref_id, resource_kind),
    CONSTRAINT fk_album_resources_owner FOREIGN KEY (album_source_ref_id) REFERENCES album_source_refs(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='本地封面资源，不持久化远端地址';

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_source_refs' AND column_name='source_title_pinyin'), 'SELECT 1', 'ALTER TABLE album_source_refs ADD COLUMN source_title_pinyin VARCHAR(512) COMMENT ''平台专辑拼音''');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_source_refs' AND column_name='source_song_total'), 'SELECT 1', 'ALTER TABLE album_source_refs ADD COLUMN source_song_total INT UNSIGNED NULL COMMENT ''平台声明歌曲总数，NULL 表示未知''');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_source_refs' AND column_name='total_count'), 'UPDATE album_source_refs SET source_song_total = CASE WHEN total_count REGEXP ''^[0-9]+$'' THEN CAST(total_count AS UNSIGNED) ELSE NULL END WHERE source_song_total IS NULL', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


-- 存量身份只在迁移时归一化，运行期不再依赖名称和歌手字段生成身份。
SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='albums' AND index_name='uk_albums_identity_hash'), 'ALTER TABLE albums DROP INDEX uk_albums_identity_hash', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

UPDATE albums a LEFT JOIN (
    SELECT r.album_db_id, MIN(CONCAT_WS('|', r.source, r.source_album_id)) AS identity_key
    FROM album_source_refs r LEFT JOIN migu_dalbum_details d ON d.album_source_ref_id=r.id
    GROUP BY r.album_db_id
) identities ON identities.album_db_id=a.id
SET a.identity_hash=SHA2(COALESCE(identities.identity_key, CONCAT('legacy|',a.id)),256);
UPDATE album_source_refs r JOIN albums a ON a.id=r.album_db_id
JOIN (SELECT identity_hash, MIN(id) AS keep_id FROM albums GROUP BY identity_hash) keepers ON keepers.identity_hash=a.identity_hash
SET r.album_db_id=keepers.keep_id;
DELETE a FROM albums a JOIN (SELECT identity_hash, MIN(id) AS keep_id FROM (SELECT id, identity_hash FROM albums) originals GROUP BY identity_hash) keepers ON keepers.identity_hash=a.identity_hash
WHERE a.id<>keepers.keep_id AND NOT EXISTS(SELECT 1 FROM album_source_refs r WHERE r.album_db_id=a.id);
SET @ownership_ddl := IF(NOT EXISTS (SELECT 1 FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='albums' AND index_name='uk_albums_identity_hash'), 'ALTER TABLE albums ADD UNIQUE KEY uk_albums_identity_hash(identity_hash)', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


-- 删除旧列之前保存已有专辑身份和拼音；每个阶段可在 DDL 中断后重试。
-- 旧名称规则可能让多个来源共享公共行；按来源 ID 分开，避免首次重采集时再改变公共主键。
INSERT INTO albums(title,normalized_title,release_date,identity_hash,created_at,updated_at)
SELECT a.title,a.normalized_title,a.release_date,SHA2(CONCAT_WS('|',r.source,r.source_album_id),256),a.created_at,UNIX_TIMESTAMP()
FROM album_source_refs r JOIN albums a ON a.id=r.album_db_id
ON DUPLICATE KEY UPDATE updated_at=UNIX_TIMESTAMP();
UPDATE album_source_refs r JOIN albums a ON a.identity_hash=SHA2(CONCAT_WS('|',r.source,r.source_album_id),256)
SET r.album_db_id=a.id;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_source_refs' AND column_name='source_album_id'), 'INSERT INTO albums(title,normalized_title,release_date,identity_hash,created_at,updated_at)
SELECT MIN(COALESCE(NULLIF(r.source_album_name,''''),CONCAT(''album:'',r.source_album_id))),
       MIN(COALESCE(NULLIF(r.source_album_name,''''),CONCAT(''album:'',r.source_album_id))), '''',
       SHA2(CONCAT_WS(''|'',r.source,r.source_album_id),256),UNIX_TIMESTAMP(),UNIX_TIMESTAMP()
FROM song_source_refs r WHERE NULLIF(r.source_album_id,'''') IS NOT NULL
 AND NOT EXISTS(SELECT 1 FROM album_source_refs a WHERE a.source=r.source AND a.source_album_id=r.source_album_id)
GROUP BY r.source,r.source_album_id
ON DUPLICATE KEY UPDATE updated_at=UNIX_TIMESTAMP()', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_source_refs' AND column_name='source_album_id'), 'INSERT INTO album_source_refs(album_db_id,source,source_item_key,source_album_id,source_resource_type,source_album_kind,source_title,source_title_pinyin,created_at,updated_at)
SELECT a.id,r.source,CONCAT(''album:'',r.source_album_id),r.source_album_id,''2003'',0,
       MIN(COALESCE(NULLIF(r.source_album_name,''''),CONCAT(''album:'',r.source_album_id))),MIN(NULLIF(r.source_album_pinyin,'''')),UNIX_TIMESTAMP(),UNIX_TIMESTAMP()
FROM song_source_refs r JOIN albums a ON a.identity_hash=SHA2(CONCAT_WS(''|'',r.source,r.source_album_id),256)
WHERE NULLIF(r.source_album_id,'''') IS NOT NULL
GROUP BY a.id,r.source,r.source_album_id
ON DUPLICATE KEY UPDATE source_title_pinyin=COALESCE(source_title_pinyin,VALUES(source_title_pinyin))', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_source_refs' AND column_name='source_album_id'), 'UPDATE album_source_refs a JOIN (SELECT source,source_album_id,MIN(NULLIF(source_album_pinyin,'''')) AS pinyin FROM song_source_refs WHERE NULLIF(source_album_id,'''') IS NOT NULL GROUP BY source,source_album_id) s ON s.source=a.source AND s.source_album_id=a.source_album_id SET a.source_title_pinyin=COALESCE(NULLIF(a.source_title_pinyin,''''),s.pinyin)', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

-- 关系中的其他专辑身份也必须先成为实体，禁止删除没有主键的旧关系。
SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_song_refs' AND column_name='source_album_id'), 'INSERT INTO albums(title,normalized_title,release_date,identity_hash,created_at,updated_at)
SELECT CONCAT(''album:'',r.source_album_id),CONCAT(''album:'',r.source_album_id),'''',SHA2(CONCAT_WS(''|'',r.source,r.source_album_id),256),UNIX_TIMESTAMP(),UNIX_TIMESTAMP()
FROM album_song_refs r WHERE NOT EXISTS(SELECT 1 FROM album_source_refs a WHERE a.source=r.source AND a.source_album_id=r.source_album_id)
GROUP BY r.source,r.source_album_id ON DUPLICATE KEY UPDATE updated_at=UNIX_TIMESTAMP()', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_song_refs' AND column_name='source_album_id'), 'INSERT INTO album_source_refs(album_db_id,source,source_item_key,source_album_id,source_resource_type,source_album_kind,source_title,created_at,updated_at)
SELECT a.id,r.source,CONCAT(''album:'',r.source_album_id),r.source_album_id,''2003'',0,a.title,UNIX_TIMESTAMP(),UNIX_TIMESTAMP()
FROM album_song_refs r JOIN albums a ON a.identity_hash=SHA2(CONCAT_WS(''|'',r.source,r.source_album_id),256)
WHERE NOT EXISTS(SELECT 1 FROM album_source_refs ar WHERE ar.source=r.source AND ar.source_album_id=r.source_album_id)
GROUP BY a.id,r.source,r.source_album_id,a.title ON DUPLICATE KEY UPDATE updated_at=UNIX_TIMESTAMP()', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_song_refs' AND column_name='source_album_id'), 'UPDATE album_song_refs r JOIN album_source_refs a ON a.source=r.source AND a.source_album_id=r.source_album_id SET r.album_source_ref_id=a.id', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_source_refs' AND column_name='source_artist_id'), 'INSERT INTO album_artist_refs(album_source_ref_id,singer_source_ref_id,artist_role,artist_order,is_primary,is_seed,created_at,updated_at)
SELECT a.id,s.id,''artist'',1,1,0,UNIX_TIMESTAMP(),UNIX_TIMESTAMP()
FROM album_source_refs a JOIN singer_source_refs s ON s.source=a.source AND s.source_singer_id=a.source_artist_id
ON DUPLICATE KEY UPDATE updated_at=VALUES(updated_at)', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS(SELECT 1 FROM information_schema.referential_constraints WHERE constraint_schema=DATABASE() AND table_name='album_song_refs' AND constraint_name='fk_album_song_refs_album_source'), 'ALTER TABLE album_song_refs DROP FOREIGN KEY fk_album_song_refs_album_source', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

ALTER TABLE album_song_refs MODIFY COLUMN album_source_ref_id BIGINT UNSIGNED NOT NULL COMMENT '关联 album_source_refs.id，写入关系前实体必须存在';
SET @ownership_ddl := IF(NOT EXISTS(SELECT 1 FROM information_schema.referential_constraints WHERE constraint_schema=DATABASE() AND table_name='album_song_refs' AND constraint_name='fk_album_song_refs_album_source'), 'ALTER TABLE album_song_refs ADD CONSTRAINT fk_album_song_refs_album_source FOREIGN KEY(album_source_ref_id) REFERENCES album_source_refs(id) ON DELETE CASCADE', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='song_source_refs' AND index_name='idx_source_album'), 'ALTER TABLE song_source_refs DROP INDEX idx_source_album', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='albums' AND index_name='idx_albums_artist'), 'ALTER TABLE albums DROP INDEX idx_albums_artist', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='album_source_refs' AND index_name='idx_album_source_artist'), 'ALTER TABLE album_source_refs DROP INDEX idx_album_source_artist', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='album_song_refs' AND index_name='uk_album_song_source_ref'), 'ALTER TABLE album_song_refs DROP INDEX uk_album_song_source_ref', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='album_song_refs' AND index_name='idx_album_song_album'), 'ALTER TABLE album_song_refs DROP INDEX idx_album_song_album', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_source_refs' AND column_name='source_album_id'), 'ALTER TABLE song_source_refs DROP COLUMN source_album_id', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_source_refs' AND column_name='source_album_name'), 'ALTER TABLE song_source_refs DROP COLUMN source_album_name', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_source_refs' AND column_name='source_album_pinyin'), 'ALTER TABLE song_source_refs DROP COLUMN source_album_pinyin', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_source_refs' AND column_name='cover_url'), 'ALTER TABLE song_source_refs DROP COLUMN cover_url', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_source_refs' AND column_name='lyric_url'), 'ALTER TABLE song_source_refs DROP COLUMN lyric_url', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_source_refs' AND column_name='mrc_url'), 'ALTER TABLE song_source_refs DROP COLUMN mrc_url', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_source_refs' AND column_name='trc_url'), 'ALTER TABLE song_source_refs DROP COLUMN trc_url', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='song_resources' AND column_name='remote_url'), 'ALTER TABLE song_resources DROP COLUMN remote_url', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='albums' AND column_name='primary_artist_name'), 'ALTER TABLE albums DROP COLUMN primary_artist_name', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_source_refs' AND column_name='source_artist_id'), 'ALTER TABLE album_source_refs DROP COLUMN source_artist_id', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_source_refs' AND column_name='source_artist_name'), 'ALTER TABLE album_source_refs DROP COLUMN source_artist_name', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_source_refs' AND column_name='source_res_type'), 'ALTER TABLE album_source_refs DROP COLUMN source_res_type', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_source_refs' AND column_name='source_action'), 'ALTER TABLE album_source_refs DROP COLUMN source_action', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_source_refs' AND column_name='cover_url'), 'ALTER TABLE album_source_refs DROP COLUMN cover_url', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_source_refs' AND column_name='total_count'), 'ALTER TABLE album_source_refs DROP COLUMN total_count', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_source_refs' AND column_name='release_date'), 'ALTER TABLE album_source_refs DROP COLUMN release_date', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_source_refs' AND column_name='publish_date'), 'ALTER TABLE album_source_refs DROP COLUMN publish_date', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_source_refs' AND column_name='play_num'), 'ALTER TABLE album_source_refs DROP COLUMN play_num', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_source_refs' AND column_name='play_num_desc'), 'ALTER TABLE album_source_refs DROP COLUMN play_num_desc', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='singer_source_refs' AND column_name='avatar_url'), 'ALTER TABLE singer_source_refs DROP COLUMN avatar_url', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='singer_source_refs' AND column_name='source_tag'), 'ALTER TABLE singer_source_refs DROP COLUMN source_tag', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_song_refs' AND column_name='source'), 'ALTER TABLE album_song_refs DROP COLUMN source', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_song_refs' AND column_name='source_song_id'), 'ALTER TABLE album_song_refs DROP COLUMN source_song_id', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_song_refs' AND column_name='source_album_id'), 'ALTER TABLE album_song_refs DROP COLUMN source_album_id', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='album_song_refs' AND column_name='source_content_id'), 'ALTER TABLE album_song_refs DROP COLUMN source_content_id', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


SET @ownership_ddl := IF(NOT EXISTS (SELECT 1 FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='album_song_refs' AND index_name='uk_album_song_relation'), 'ALTER TABLE album_song_refs ADD UNIQUE KEY uk_album_song_relation(album_source_ref_id,song_source_ref_id)', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(NOT EXISTS (SELECT 1 FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='album_song_refs' AND index_name='idx_album_song_order'), 'ALTER TABLE album_song_refs ADD KEY idx_album_song_order(album_source_ref_id,disc_no,track_no,song_order)', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;




-- 数字专辑只维护 materialId 映射，售卖和会员字段不属于采集模块。
SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND index_name='uk_migu_dalbum_source_album'), 'ALTER TABLE migu_dalbum_details DROP INDEX uk_migu_dalbum_source_album', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND index_name='idx_migu_dalbum_sale'), 'ALTER TABLE migu_dalbum_details DROP INDEX idx_migu_dalbum_sale', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND index_name='idx_migu_dalbum_period'), 'ALTER TABLE migu_dalbum_details DROP INDEX idx_migu_dalbum_period', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='source_album_id'), 'ALTER TABLE migu_dalbum_details DROP COLUMN source_album_id', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='copyright_id'), 'ALTER TABLE migu_dalbum_details DROP COLUMN copyright_id', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='price'), 'ALTER TABLE migu_dalbum_details DROP COLUMN price', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='online_date'), 'ALTER TABLE migu_dalbum_details DROP COLUMN online_date', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='source_item_id'), 'ALTER TABLE migu_dalbum_details DROP COLUMN source_item_id', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='source_campaign_id'), 'ALTER TABLE migu_dalbum_details DROP COLUMN source_campaign_id', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='presale_date'), 'ALTER TABLE migu_dalbum_details DROP COLUMN presale_date', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='first_start_date'), 'ALTER TABLE migu_dalbum_details DROP COLUMN first_start_date', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='first_end_date'), 'ALTER TABLE migu_dalbum_details DROP COLUMN first_end_date', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='sale_end_date'), 'ALTER TABLE migu_dalbum_details DROP COLUMN sale_end_date', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='invalidate_date'), 'ALTER TABLE migu_dalbum_details DROP COLUMN invalidate_date', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='original_first_start_date'), 'ALTER TABLE migu_dalbum_details DROP COLUMN original_first_start_date', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='original_first_end_date'), 'ALTER TABLE migu_dalbum_details DROP COLUMN original_first_end_date', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='digital_period_status'), 'ALTER TABLE migu_dalbum_details DROP COLUMN digital_period_status', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='vip_type'), 'ALTER TABLE migu_dalbum_details DROP COLUMN vip_type', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='vip_flag'), 'ALTER TABLE migu_dalbum_details DROP COLUMN vip_flag', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='vip_start_time'), 'ALTER TABLE migu_dalbum_details DROP COLUMN vip_start_time', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='vip_end_time'), 'ALTER TABLE migu_dalbum_details DROP COLUMN vip_end_time', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='effective_status'), 'ALTER TABLE migu_dalbum_details DROP COLUMN effective_status', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='convert_status'), 'ALTER TABLE migu_dalbum_details DROP COLUMN convert_status', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='forever_listen_flag'), 'ALTER TABLE migu_dalbum_details DROP COLUMN forever_listen_flag', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='forever_listen'), 'ALTER TABLE migu_dalbum_details DROP COLUMN forever_listen', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='is_before_sale_end'), 'ALTER TABLE migu_dalbum_details DROP COLUMN is_before_sale_end', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='is_in_first_date'), 'ALTER TABLE migu_dalbum_details DROP COLUMN is_in_first_date', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='is_call_payment'), 'ALTER TABLE migu_dalbum_details DROP COLUMN is_call_payment', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='is_call_payment_for_present'), 'ALTER TABLE migu_dalbum_details DROP COLUMN is_call_payment_for_present', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='is_call_payment_for_purchase'), 'ALTER TABLE migu_dalbum_details DROP COLUMN is_call_payment_for_purchase', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='is_knowledge_paid'), 'ALTER TABLE migu_dalbum_details DROP COLUMN is_knowledge_paid', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='have_filter_tag'), 'ALTER TABLE migu_dalbum_details DROP COLUMN have_filter_tag', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='is_convert'), 'ALTER TABLE migu_dalbum_details DROP COLUMN is_convert', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;

SET @ownership_ddl := IF(EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='migu_dalbum_details' AND column_name='is_appointment'), 'ALTER TABLE migu_dalbum_details DROP COLUMN is_appointment', 'SELECT 1');
PREPARE ownership_stmt FROM @ownership_ddl;
EXECUTE ownership_stmt;
DEALLOCATE PREPARE ownership_stmt;


CREATE OR REPLACE VIEW album_song_catalog AS
SELECT r.id,r.album_source_ref_id,r.song_source_ref_id,a.source,a.source_album_id,
       s.source_song_id,s.source_content_id,r.disc_no,r.disc_name,r.track_no,r.song_order,r.created_at,r.updated_at
FROM album_song_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id
JOIN song_source_refs s ON s.id=r.song_source_ref_id;

CREATE OR REPLACE VIEW song_catalog_sources AS
SELECT r.*,
       COALESCE((SELECT a.source_album_id FROM album_song_refs ar JOIN album_source_refs a ON a.id=ar.album_source_ref_id WHERE ar.song_source_ref_id=r.id ORDER BY ar.id LIMIT 1),'') AS source_album_id,
       COALESCE((SELECT a.source_title FROM album_song_refs ar JOIN album_source_refs a ON a.id=ar.album_source_ref_id WHERE ar.song_source_ref_id=r.id ORDER BY ar.id LIMIT 1),'') AS source_album_name
FROM song_source_refs r;

CREATE OR REPLACE VIEW album_catalog_sources AS
SELECT r.*,COALESCE((SELECT s.source_singer_id FROM album_artist_refs ar JOIN singer_source_refs s ON s.id=ar.singer_source_ref_id WHERE ar.album_source_ref_id=r.id ORDER BY ar.is_primary DESC,ar.is_seed DESC,ar.artist_order,ar.id LIMIT 1),'') AS source_artist_id,COALESCE((SELECT s.name FROM album_artist_refs ar JOIN singer_source_refs sr ON sr.id=ar.singer_source_ref_id JOIN singers s ON s.id=sr.singer_db_id WHERE ar.album_source_ref_id=r.id ORDER BY ar.is_primary DESC,ar.is_seed DESC,ar.artist_order,ar.id LIMIT 1),'') AS source_artist_name,
       CASE WHEN r.source_resource_type='5' THEN '1' ELSE '0' END AS source_res_type,
       COALESCE(CAST(r.source_song_total AS CHAR),'') AS total_count,
       a.release_date AS release_date,a.release_date AS publish_date,0 AS play_num,'' AS play_num_desc
FROM album_source_refs r JOIN albums a ON a.id=r.album_db_id;

CREATE OR REPLACE VIEW singer_catalog_sources AS
SELECT r.*,COALESCE((SELECT m.source_tag FROM singer_index_memberships m WHERE m.singer_source_ref_id=r.id AND NULLIF(m.source_tag,'') IS NOT NULL ORDER BY m.id LIMIT 1),'') AS source_tag
FROM singer_source_refs r;

CREATE OR REPLACE VIEW album_catalog AS
SELECT a.*,COALESCE((SELECT s.name FROM album_source_refs ar JOIN album_artist_refs aa ON aa.album_source_ref_id=ar.id JOIN singer_source_refs sr ON sr.id=aa.singer_source_ref_id JOIN singers s ON s.id=sr.singer_db_id WHERE ar.album_db_id=a.id ORDER BY aa.is_primary DESC,aa.artist_order,aa.id LIMIT 1),'') AS primary_artist_name
FROM albums a;

ALTER TABLE migu_dalbum_details COMMENT='Migu 数字专辑物料身份映射';

ALTER TABLE song_source_audio_formats
MODIFY COLUMN iformat VARCHAR(32) NULL COMMENT 'iOS 资源格式编码',
MODIFY COLUMN aformat VARCHAR(32) NULL COMMENT 'Android 资源格式编码',
MODIFY COLUMN isize BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'iOS 资源大小，单位字节；未知为 0',
MODIFY COLUMN asize BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Android 资源大小，单位字节；未知为 0';

INSERT INTO crawler_schema_versions(component,schema_version,schema_name,applied_at,updated_at)
VALUES('crawler_mysql',2026102501,'normalized music table ownership',UNIX_TIMESTAMP(),UNIX_TIMESTAMP())
ON DUPLICATE KEY UPDATE schema_version=GREATEST(schema_version,VALUES(schema_version)),schema_name=VALUES(schema_name),updated_at=VALUES(updated_at);
