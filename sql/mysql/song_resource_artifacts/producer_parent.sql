SELECT a.id FROM song_resource_artifacts a
JOIN song_resources s ON s.id=a.source_resource_id
WHERE a.id=? AND a.source_resource_id=? AND s.resource_kind='mrc'
 AND a.artifact_type IN ('word_json','enhanced_lrc')
