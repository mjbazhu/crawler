SELECT COUNT(*) FROM resource_transfer_tasks WHERE operation_id=? AND destination=? AND state IN('pending','retry','running','waiting_input','uncertain')
