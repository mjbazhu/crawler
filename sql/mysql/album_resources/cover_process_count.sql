SELECT COUNT(*) FROM album_resources r JOIN album_source_refs s ON s.id=r.album_source_ref_id
WHERE s.source=? AND (r.status IN (0,3) OR (r.status=2 AND
    (r.cloud_status IN (0,3,4) OR (r.cloud_status=2 AND
     (r.cloud_backend<>? OR BINARY COALESCE(r.cloud_path,'')<>BINARY CONCAT(?,r.local_path)
      OR r.cloud_size_bytes<>r.size_bytes OR BINARY r.cloud_checksum<>BINARY COALESCE(r.checksum,'')))))) /*__FILTERS__*/;
