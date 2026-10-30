-- 旧工具仅报告完成，不能把沿用的本地/大小指纹写成远端强核验。
INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,song_resource_id,
 task_state,content_generation,target_revision,cloud_path,size_bytes,source_adapter,
 verification,verification_method,evidence,path_unresolved,uploaded_at,created_at,updated_at)
VALUES('song',?,?,?,'succeeded',?,?,?,?,?,'unverified','legacy_adapter_report',CAST(? AS JSON),0,UNIX_TIMESTAMP(),UNIX_TIMESTAMP(),UNIX_TIMESTAMP())
ON DUPLICATE KEY UPDATE task_state='succeeded',content_generation=VALUES(content_generation),target_revision=VALUES(target_revision),
 cloud_path=VALUES(cloud_path),size_bytes=VALUES(size_bytes),source_adapter=VALUES(source_adapter),
 checksum='',verification='unverified',verification_method='legacy_adapter_report',path_unresolved=0,
 evidence=JSON_SET(COALESCE(evidence,JSON_OBJECT()),'$.legacy_song_writeback',JSON_EXTRACT(VALUES(evidence),'$.legacy_song_writeback')),
 uploaded_at=UNIX_TIMESTAMP(),updated_at=UNIX_TIMESTAMP();
