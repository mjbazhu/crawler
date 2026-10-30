SELECT COUNT(*)
FROM singer_resources r JOIN singer_source_refs s ON s.id=r.singer_source_ref_id
JOIN resource_destination_states d ON d.destination=?
LEFT JOIN resource_cloud_receipts c ON c.owner_type='singer' AND c.resource_id=r.id AND c.cloud_provider=d.destination
WHERE s.source=? /*__FILTERS__*/
;
