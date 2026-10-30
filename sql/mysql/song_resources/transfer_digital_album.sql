UPDATE song_resources resource JOIN song_resources origin ON resource.song_source_ref_id=origin.song_source_ref_id
SET resource.status=4,resource.claim_generation=resource.claim_generation+1,resource.running_batch_id=NULL,
 resource.result_code=?,resource.error_category='copyright',resource.last_error=NULL,resource.updated_at=UNIX_TIMESTAMP()
WHERE origin.id=? AND origin.content_generation=? AND origin.source_revision=? AND origin.resource_kind='audio'
 AND resource.resource_kind='audio' AND resource.status IN (0,1,3)
