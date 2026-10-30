SELECT p.producer_id FROM resource_producer_tasks p FORCE INDEX(idx_producer_dispatch)
STRAIGHT_JOIN resource_transfer_tasks t ON t.task_id=p.parent_task_id
WHERE p.destination IN('local','baidu','gdrive') AND p.state IN('pending','running','retry','waiting_input')
 AND t.owner_type=? AND t.resource_id=? ORDER BY p.producer_id FOR UPDATE
