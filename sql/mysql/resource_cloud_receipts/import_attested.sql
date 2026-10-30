INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,
    song_resource_id,singer_resource_id,album_resource_id,artifact_resource_id,
    task_state,content_generation,target_revision,transfer_policy,policy_content_generation,policy_target_revision,
    baseline_id,skip_reason,verification,cloud_path,path_unresolved,source_adapter,evidence,created_at,updated_at)
VALUES(?,?,?,NULLIF(?,0),NULLIF(?,0),NULLIF(?,0),NULLIF(?,0),
    'skipped',?,?,'skip',?,?,?,'legacy_both_clouds_present','operator_attested',NULLIF(?,''),?,?,CAST(? AS JSON),UNIX_TIMESTAMP(),UNIX_TIMESTAMP())
ON DUPLICATE KEY UPDATE
    transfer_policy='skip',policy_content_generation=VALUES(policy_content_generation),policy_target_revision=VALUES(policy_target_revision),
    baseline_id=VALUES(baseline_id),skip_reason=VALUES(skip_reason),
    cloud_path=IF(COALESCE(cloud_path,'')='' AND confirmed_content_generation=0,VALUES(cloud_path),cloud_path),
    path_unresolved=IF(confirmed_content_generation=0,IF(COALESCE(cloud_path,'')='',1,0),path_unresolved),
    source_adapter=IF(source_adapter='' AND confirmed_content_generation=0,VALUES(source_adapter),source_adapter),
    task_state=IF(verification IN ('hash_matched','sha256_verified') AND confirmed_content_generation=VALUES(content_generation) AND confirmed_target_revision=VALUES(target_revision),'succeeded','skipped'),
    content_generation=VALUES(content_generation),target_revision=VALUES(target_revision),
    verification=IF(verification='unverified','operator_attested',verification),
    evidence=IF(JSON_EXTRACT(VALUES(evidence),'$.retired_legacy') IS NULL OR JSON_CONTAINS_PATH(COALESCE(evidence,JSON_OBJECT()),'one','$.retired_legacy'),
      JSON_SET(COALESCE(evidence,JSON_OBJECT()),'$.legacy_import',VALUES(evidence)),
      JSON_SET(COALESCE(evidence,JSON_OBJECT()),'$.legacy_import',VALUES(evidence),'$.retired_legacy',JSON_EXTRACT(VALUES(evidence),'$.retired_legacy'))),
    updated_at=UNIX_TIMESTAMP();
