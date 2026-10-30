SELECT r.id,r.album_source_ref_id,s.source,s.source_album_id,COALESCE(r.local_path,''),r.size_bytes,COALESCE(r.checksum,''),r.status,
 COALESCE(c.source_adapter,''),COALESCE(c.cloud_path,''),COALESCE(c.checksum,''),CAST(COALESCE(c.size_bytes,0) AS UNSIGNED),
 CAST(COALESCE(c.uploaded_at,0) AS UNSIGNED),COALESCE(c.task_state,'pending'),COALESCE(c.verification,'unverified'),
 CAST(COALESCE(c.confirmed_content_generation,0) AS UNSIGNED),CAST(COALESCE(c.confirmed_target_revision,0) AS UNSIGNED),r.content_generation,
 d.target_revision,r.reference_sha256,r.reference_size_bytes,COALESCE(c.result_code,''),COALESCE(c.error_category,''),COALESCE(c.path_unresolved,1),
 CAST((/*__CLOUD_CURRENT__*/) AS UNSIGNED),CAST((/*__CLOUD_STATUS__*/) AS UNSIGNED)
FROM album_resources r JOIN album_source_refs s ON s.id=r.album_source_ref_id
JOIN resource_destination_states d ON d.destination=?
LEFT JOIN resource_cloud_receipts c ON c.owner_type='album' AND c.resource_id=r.id AND c.cloud_provider=d.destination
WHERE s.source=? /*__FILTERS__*/ ORDER BY r.updated_at,r.id LIMIT ? OFFSET ?
