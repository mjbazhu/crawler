SELECT r.id,r.owner_type,r.resource_id,r.cloud_provider,r.source_adapter,r.task_state,
 r.confirmed_content_generation,r.confirmed_target_revision,r.size_bytes,r.checksum,
 r.verification,r.verification_method,r.uploaded_at,r.verified_at,r.result_code,
 CAST(COALESCE(r.cloud_path,'')<>'' AND r.path_unresolved=0 AS UNSIGNED),
 CAST(COALESCE(s.content_generation,si.content_generation,a.content_generation,ar.content_generation,0) AS UNSIGNED),d.target_revision,
 COALESCE(s.reference_sha256,si.reference_sha256,a.reference_sha256,ar.reference_sha256,''),
 CAST(COALESCE(s.reference_size_bytes,si.reference_size_bytes,a.reference_size_bytes,ar.reference_size_bytes,0) AS UNSIGNED),
 r.transfer_policy,r.policy_content_generation,r.policy_target_revision,r.baseline_id,r.skip_reason,
 COALESCE(s.checksum,si.checksum,a.checksum,ar.checksum,''),
 CAST(COALESCE(s.local_content_generation,si.local_content_generation,a.local_content_generation,ar.local_content_generation,0) AS UNSIGNED),
 CAST(COALESCE(s.local_target_revision,si.local_target_revision,a.local_target_revision,ar.local_target_revision,0) AS UNSIGNED),ld.target_revision,
 CAST(COALESCE(s.size_bytes,si.size_bytes,a.size_bytes,ar.size_bytes,0) AS UNSIGNED)
FROM resource_cloud_receipts r
JOIN resource_destination_states d ON d.destination=r.cloud_provider
JOIN resource_destination_states ld ON ld.destination='local'
LEFT JOIN song_resources s ON r.owner_type='song' AND s.id=r.resource_id
LEFT JOIN singer_resources si ON r.owner_type='singer' AND si.id=r.resource_id
LEFT JOIN album_resources a ON r.owner_type='album' AND a.id=r.resource_id
LEFT JOIN song_resource_artifacts ar ON r.owner_type='song_artifact' AND ar.id=r.resource_id
WHERE r.owner_type=? AND (?=0 OR r.resource_id=?) AND (?='' OR r.cloud_provider=?)
 AND (?='' OR r.task_state=?) AND (?='' OR r.verification=?) AND r.id>?
ORDER BY r.id LIMIT ?
