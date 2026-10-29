SELECT COUNT(*) FROM singer_resources r JOIN singer_source_refs s ON s.id=r.singer_source_ref_id WHERE s.source=? /*__FILTERS__*/;
