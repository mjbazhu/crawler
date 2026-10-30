SELECT task_id FROM resource_transfer_tasks FORCE INDEX(idx_transfer_dispatch) WHERE destination=? AND state IN('pending','retry','running','waiting_input','uncertain') LIMIT ?
