SELECT COUNT(*)
FROM album_resources r JOIN album_source_refs s ON s.id=r.album_source_ref_id
JOIN resource_destination_states d ON d.destination=?
LEFT JOIN resource_cloud_receipts c ON c.owner_type='album' AND c.resource_id=r.id AND c.cloud_provider=d.destination
WHERE s.source=? AND (r.status IN (0,3) OR (r.status=2 AND
    ((CASE c.task_state WHEN 'running' THEN 1 WHEN 'uncertain' THEN 4 WHEN 'failed' THEN 3 WHEN 'succeeded' THEN IF(c.confirmed_content_generation=r.content_generation AND c.confirmed_target_revision=d.target_revision AND c.size_bytes=r.size_bytes AND BINARY c.checksum=BINARY COALESCE(r.checksum,''),2,0) ELSE 0 END) IN (0,3,4) OR ((CASE c.task_state WHEN 'running' THEN 1 WHEN 'uncertain' THEN 4 WHEN 'failed' THEN 3 WHEN 'succeeded' THEN IF(c.confirmed_content_generation=r.content_generation AND c.confirmed_target_revision=d.target_revision AND c.size_bytes=r.size_bytes AND BINARY c.checksum=BINARY COALESCE(r.checksum,''),2,0) ELSE 0 END)=2 AND
     (c.source_adapter<>? OR BINARY COALESCE(c.cloud_path,'')<>BINARY CONCAT(?,r.local_path)
      OR c.size_bytes<>r.size_bytes OR BINARY c.checksum<>BINARY COALESCE(r.checksum,'')))))) /*__FILTERS__*/
;
