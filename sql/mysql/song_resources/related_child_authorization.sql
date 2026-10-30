SELECT child.id FROM song_resources parent JOIN song_resources child ON child.song_source_ref_id=parent.song_source_ref_id
JOIN resource_transfer_related discovered ON discovered.parent_task_id=?
WHERE parent.id=? AND parent.resource_kind='audio' AND child.id=?
AND (discovered.discovered_mask & CASE child.resource_kind WHEN 'cover' THEN 1 WHEN 'lyric' THEN 2 WHEN 'mrc' THEN 4 WHEN 'trc' THEN 8 ELSE 0 END)<>0
