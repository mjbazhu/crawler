SELECT (SELECT COUNT(*) FROM resource_transfer_slots)+(SELECT COUNT(*) FROM resource_transfer_tasks WHERE state IN ('pending','retry','running','waiting_input','uncertain'))+
(SELECT COUNT(*) FROM resource_producer_tasks WHERE state IN ('pending','retry','running','waiting_input'))+
(SELECT COUNT(*) FROM resource_cloud_receipts WHERE task_state IN ('running','uncertain') OR attempt IS NOT NULL OR running_batch_id<>'')+
(SELECT COUNT(*) FROM song_resources WHERE status=1)+(SELECT COUNT(*) FROM singer_resources WHERE status=1)+
(SELECT COUNT(*) FROM album_resources WHERE status=1)+(SELECT COUNT(*) FROM song_resource_artifacts WHERE status=1)
