SELECT task_id FROM resource_transfer_tasks FORCE INDEX(idx_transfer_dispatch)
WHERE destination IN('local','baidu','gdrive') AND state IN('pending','running','retry','waiting_input','uncertain')
 AND owner_type=? AND resource_id=? ORDER BY task_id FOR UPDATE
