INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,song_resource_id,singer_resource_id,album_resource_id,artifact_resource_id,
 content_generation,target_revision,transfer_policy,policy_content_generation,policy_target_revision,baseline_id,skip_reason,task_state,verification,cloud_path,path_unresolved,evidence,created_at,updated_at)
VALUES(?,?,?,NULLIF(?,0),NULLIF(?,0),NULLIF(?,0),NULLIF(?,0),?,?,?,?,?,?,?,?,?,NULLIF(?,''),0,CAST(? AS JSON),UNIX_TIMESTAMP(),UNIX_TIMESTAMP())
ON DUPLICATE KEY UPDATE transfer_policy=VALUES(transfer_policy),policy_content_generation=VALUES(policy_content_generation),policy_target_revision=VALUES(policy_target_revision),
 baseline_id=VALUES(baseline_id),skip_reason=VALUES(skip_reason),content_generation=VALUES(content_generation),target_revision=VALUES(target_revision),
 cloud_path=IF(confirmed_content_generation=0 AND COALESCE(cloud_path,'')='',VALUES(cloud_path),cloud_path),
 path_unresolved=IF(confirmed_content_generation=0,IF(COALESCE(cloud_path,'')='',1,0),path_unresolved),
 verification=IF(verification='unverified' AND VALUES(transfer_policy)='skip','operator_attested',verification),
 task_state=IF(VALUES(transfer_policy)='skip' AND NOT(confirmed_content_generation=VALUES(content_generation) AND confirmed_target_revision=VALUES(target_revision) AND verification IN('sha256_verified','hash_matched')),'skipped',task_state),
 evidence=JSON_SET(COALESCE(evidence,JSON_OBJECT()),'$.history_adoption',VALUES(evidence)),updated_at=UNIX_TIMESTAMP();
