SELECT task_id,operation_id FROM (
    (SELECT task_id,operation_id,available_at FROM resource_transfer_tasks
     WHERE destination=? AND state='pending' AND available_at<=UNIX_TIMESTAMP()
     ORDER BY available_at,task_id LIMIT 1)
    UNION ALL
    (SELECT task_id,operation_id,available_at FROM resource_transfer_tasks
     WHERE destination=? AND state='retry' AND available_at<=UNIX_TIMESTAMP()
     ORDER BY available_at,task_id LIMIT 1)
) candidates ORDER BY available_at,task_id LIMIT 1
