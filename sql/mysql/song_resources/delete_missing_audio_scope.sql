SELECT id FROM song_resources
WHERE song_source_ref_id=? AND resource_kind='audio' AND format_type=''
  AND status=4 AND result_code='missing_audio_formats' AND COALESCE(local_path,'')=''
ORDER BY id FOR UPDATE
