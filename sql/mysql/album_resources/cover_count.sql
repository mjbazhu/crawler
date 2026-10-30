SELECT COUNT(*)
FROM album_resources r JOIN album_source_refs s ON s.id=r.album_source_ref_id
JOIN resource_destination_states d ON d.destination=?
LEFT JOIN resource_cloud_receipts c ON c.owner_type='album' AND c.resource_id=r.id AND c.cloud_provider=d.destination
WHERE s.source=? /*__FILTERS__*/
;
