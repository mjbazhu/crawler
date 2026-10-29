SELECT r.id, r.singer_source_ref_id, s.source, s.source_singer_id, COALESCE(r.remote_url,''),
       COALESCE(r.local_path,''), r.size_bytes, COALESCE(r.checksum,''), r.status,
       r.error_category, r.result_code, COALESCE(r.last_error,''),
       r.claim_generation, r.claimed_at, COALESCE(r.running_batch_id,''),
       r.cloud_backend, COALESCE(r.cloud_path,''), r.cloud_size_bytes, r.cloud_checksum, r.cloud_status,
       r.cloud_uploaded_at, r.cloud_claim_generation, r.cloud_claimed_at, COALESCE(r.cloud_running_batch_id,''),
       r.cloud_error_category, r.cloud_result_code, COALESCE(r.cloud_last_error,'')
FROM singer_resources r JOIN singer_source_refs s ON s.id=r.singer_source_ref_id
WHERE s.source=? /*__FILTERS__*/
ORDER BY r.updated_at, r.id LIMIT ? OFFSET ?;
