SELECT task_id FROM resource_transfer_slots WHERE owner_type=? AND resource_id=? ORDER BY destination FOR UPDATE
