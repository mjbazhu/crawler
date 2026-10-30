SELECT COUNT(*) FROM resource_producer_tasks WHERE parent_operation_id=? AND destination=? AND state IN('pending','retry','running','waiting_input')
