SELECT task_id FROM resource_transfer_slots WHERE owner_type='song' AND resource_id=? ORDER BY destination FOR UPDATE;
