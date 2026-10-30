SELECT r.id,r.album_source_ref_id,s.source,s.source_album_id,COALESCE(r.remote_url,''),
       COALESCE(r.local_path,''),r.size_bytes,COALESCE(r.checksum,''),r.status,
       r.error_category,r.result_code,COALESCE(r.last_error,''),r.claim_generation,r.claimed_at,COALESCE(r.running_batch_id,''),
       COALESCE(c.source_adapter,''),COALESCE(c.cloud_path,''),CAST(COALESCE(c.size_bytes,0) AS UNSIGNED),COALESCE(c.checksum,''),
       CASE c.task_state WHEN 'running' THEN 1 WHEN 'uncertain' THEN 4 WHEN 'failed' THEN 3 WHEN 'succeeded' THEN IF(c.confirmed_content_generation=r.content_generation AND c.confirmed_target_revision=d.target_revision AND c.size_bytes=r.size_bytes AND BINARY c.checksum=BINARY COALESCE(r.checksum,''),2,0) ELSE 0 END,CAST(COALESCE(c.uploaded_at,0) AS UNSIGNED),CAST(COALESCE(c.claim_generation,0) AS UNSIGNED),CAST(COALESCE(c.claimed_at,0) AS UNSIGNED),COALESCE(c.running_batch_id,''),
       COALESCE(c.error_category,''),COALESCE(c.result_code,''),COALESCE(c.last_error,''),r.content_generation,d.target_revision,r.source_revision
FROM album_resources r JOIN album_source_refs s ON s.id=r.album_source_ref_id
JOIN resource_destination_states d ON d.destination=?
LEFT JOIN resource_cloud_receipts c ON c.owner_type='album' AND c.resource_id=r.id AND c.cloud_provider=d.destination
WHERE s.source=? /*__FILTERS__*/
ORDER BY r.updated_at,r.id LIMIT ? OFFSET ?;
