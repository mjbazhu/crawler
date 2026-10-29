SELECT COUNT(*) FROM album_resources r JOIN album_source_refs s ON s.id=r.album_source_ref_id WHERE s.source=? /*__FILTERS__*/;
