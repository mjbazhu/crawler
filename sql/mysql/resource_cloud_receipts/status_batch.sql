WITH wanted AS (
 SELECT * FROM JSON_TABLE(?, '$[*]' COLUMNS(ordinal FOR ORDINALITY, owner_type VARCHAR(16) PATH '$.owner_type',resource_id BIGINT UNSIGNED PATH '$.resource_id')) j
), resources AS (
 SELECT w.ordinal,
 w.owner_type,
 w.resource_id,
 COALESCE(s.id,si.id,a.id,ar.id,0) AS id,
 COALESCE(s.content_generation,si.content_generation,a.content_generation,ar.content_generation,0) AS content_generation,
 COALESCE(s.local_content_generation,si.local_content_generation,a.local_content_generation,ar.local_content_generation,0) AS local_content_generation,
 COALESCE(s.local_target_revision,si.local_target_revision,a.local_target_revision,ar.local_target_revision,0) AS local_target_revision,
 COALESCE(s.local_skip_generation,si.local_skip_generation,a.local_skip_generation,ar.local_skip_generation,0) AS local_skip_generation,
 COALESCE(s.local_skip_target_revision,si.local_skip_target_revision,a.local_skip_target_revision,ar.local_skip_target_revision,0) AS local_skip_target_revision,
 COALESCE(s.local_skip_baseline,si.local_skip_baseline,a.local_skip_baseline,ar.local_skip_baseline,'') AS local_skip_baseline,
 COALESCE(s.local_skip_reason,si.local_skip_reason,a.local_skip_reason,ar.local_skip_reason,'') AS local_skip_reason,
 COALESCE(s.local_path,si.local_path,a.local_path,ar.local_path,'') AS local_path,
 COALESCE(s.checksum,si.checksum,a.checksum,ar.checksum,'') AS checksum,
 COALESCE(s.size_bytes,si.size_bytes,a.size_bytes,ar.size_bytes,0) AS size_bytes,
 COALESCE(s.reference_sha256,si.reference_sha256,a.reference_sha256,ar.reference_sha256,'') AS reference_sha256,
 COALESCE(s.reference_size_bytes,si.reference_size_bytes,a.reference_size_bytes,ar.reference_size_bytes,0) AS reference_size_bytes,
 COALESCE(s.status,si.status,a.status,ar.status,0) AS status
 FROM wanted w
 LEFT JOIN song_resources s ON w.owner_type='song' AND s.id=w.resource_id
 LEFT JOIN singer_resources si ON w.owner_type='singer' AND si.id=w.resource_id
 LEFT JOIN album_resources a ON w.owner_type='album' AND a.id=w.resource_id
 LEFT JOIN song_resource_artifacts ar ON w.owner_type='song_artifact' AND ar.id=w.resource_id
), targets AS (SELECT 'local' AS destination UNION ALL SELECT 'baidu' UNION ALL SELECT 'gdrive')
SELECT c.id,r.owner_type,r.resource_id,t.destination,COALESCE(c.source_adapter,''),COALESCE(c.task_state,'pending'),
 CAST(COALESCE(c.confirmed_content_generation,0) AS UNSIGNED),CAST(COALESCE(c.confirmed_target_revision,0) AS UNSIGNED),CAST(COALESCE(c.size_bytes,0) AS UNSIGNED),COALESCE(c.checksum,''),
 COALESCE(c.verification,'unverified'),COALESCE(c.verification_method,''),c.uploaded_at,c.verified_at,COALESCE(c.result_code,''),
 CAST(COALESCE(c.cloud_path,'')<>'' AND COALESCE(c.path_unresolved,1)=0 AS UNSIGNED),
 CAST(r.content_generation AS UNSIGNED),CAST(COALESCE(d.target_revision,0) AS UNSIGNED),r.reference_sha256,CAST(r.reference_size_bytes AS UNSIGNED),
 COALESCE(c.transfer_policy,'process'),CAST(COALESCE(c.policy_content_generation,0) AS UNSIGNED),CAST(COALESCE(c.policy_target_revision,0) AS UNSIGNED),COALESCE(c.baseline_id,''),COALESCE(c.skip_reason,''),
 r.checksum,CAST(r.local_content_generation AS UNSIGNED),CAST(r.local_target_revision AS UNSIGNED),CAST(COALESCE(ld.target_revision,0) AS UNSIGNED),CAST(r.size_bytes AS UNSIGNED),
 r.ordinal,CAST(r.id AS UNSIGNED),r.status,CAST(r.local_path<>'' AS UNSIGNED),
 CAST(r.local_skip_generation AS UNSIGNED),CAST(r.local_skip_target_revision AS UNSIGNED),r.local_skip_baseline,r.local_skip_reason
FROM resources r CROSS JOIN targets t
LEFT JOIN resource_destination_states d ON d.destination=t.destination
LEFT JOIN resource_destination_states ld ON ld.destination='local'
LEFT JOIN resource_cloud_receipts c ON t.destination<>'local' AND c.owner_type=r.owner_type AND c.resource_id=r.resource_id AND c.cloud_provider=t.destination
ORDER BY r.ordinal,t.destination
