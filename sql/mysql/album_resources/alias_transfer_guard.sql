SELECT r.id,EXISTS(SELECT 1 FROM resource_transfer_slots s WHERE s.owner_type='album' AND s.resource_id=r.id)
 OR EXISTS(SELECT 1 FROM resource_cloud_receipts c WHERE c.owner_type='album' AND c.resource_id=r.id AND (c.task_state IN ('running','uncertain') OR c.attempt IS NOT NULL OR c.running_batch_id<>''))
 OR EXISTS(SELECT 1 FROM resource_transfer_tasks t FORCE INDEX(idx_transfer_dispatch)
           WHERE t.destination IN('local','baidu','gdrive') AND t.state IN('pending','running','retry','waiting_input','uncertain') AND t.owner_type='album' AND t.resource_id=r.id)
 OR EXISTS(SELECT 1 FROM resource_producer_tasks p FORCE INDEX(idx_producer_dispatch)
           STRAIGHT_JOIN resource_transfer_tasks t ON t.task_id=p.parent_task_id
           WHERE p.destination IN('local','baidu','gdrive') AND p.state IN('pending','running','retry','waiting_input') AND t.owner_type='album' AND t.resource_id=r.id) AS busy
FROM album_resources r WHERE r.album_source_ref_id IN (?,?) ORDER BY r.id FOR UPDATE
