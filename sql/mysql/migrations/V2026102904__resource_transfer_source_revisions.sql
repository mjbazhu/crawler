-- 来源描述变更捕获；不重下既有文件，不撤销人工历史策略，不自动放行目标。
DELIMITER $$
DROP TRIGGER IF EXISTS transfer_singer_source_revision$$
CREATE TRIGGER transfer_singer_source_revision BEFORE UPDATE ON singer_resources FOR EACH ROW
BEGIN
    IF NOT (NEW.singer_source_ref_id <=> OLD.singer_source_ref_id) OR NOT (NEW.resource_kind <=> OLD.resource_kind) OR NOT (NEW.remote_url <=> OLD.remote_url) THEN
        SET NEW.source_revision=GREATEST(NEW.source_revision,OLD.source_revision+1);
        SET NEW.claim_generation=GREATEST(NEW.claim_generation,OLD.claim_generation+1),NEW.running_batch_id=NULL;
        IF NEW.status=1 THEN SET NEW.status=IF(OLD.local_content_generation>0,2,0); END IF;
        SET NEW.cloud_claim_generation=GREATEST(NEW.cloud_claim_generation,OLD.cloud_claim_generation+1),NEW.cloud_running_batch_id=NULL;
        IF NEW.cloud_status=1 THEN SET NEW.cloud_status=4; END IF;
        IF NOT (NEW.singer_source_ref_id <=> OLD.singer_source_ref_id) OR NOT (NEW.resource_kind <=> OLD.resource_kind) THEN
            SET NEW.content_generation=GREATEST(NEW.content_generation,OLD.content_generation+1);
            SET NEW.reference_sha256='',NEW.reference_size_bytes=0;
        END IF;
    END IF;
END$$

DROP TRIGGER IF EXISTS transfer_album_source_revision$$
CREATE TRIGGER transfer_album_source_revision BEFORE UPDATE ON album_resources FOR EACH ROW
BEGIN
    IF NOT (NEW.album_source_ref_id <=> OLD.album_source_ref_id) OR NOT (NEW.resource_kind <=> OLD.resource_kind) OR NOT (NEW.remote_url <=> OLD.remote_url) THEN
        SET NEW.source_revision=GREATEST(NEW.source_revision,OLD.source_revision+1);
        SET NEW.claim_generation=GREATEST(NEW.claim_generation,OLD.claim_generation+1),NEW.running_batch_id=NULL;
        IF NEW.status=1 THEN SET NEW.status=IF(OLD.local_content_generation>0,2,0); END IF;
        SET NEW.cloud_claim_generation=GREATEST(NEW.cloud_claim_generation,OLD.cloud_claim_generation+1),NEW.cloud_running_batch_id=NULL;
        IF NEW.cloud_status=1 THEN SET NEW.cloud_status=4; END IF;
        IF NOT (NEW.album_source_ref_id <=> OLD.album_source_ref_id) OR NOT (NEW.resource_kind <=> OLD.resource_kind) THEN
            SET NEW.content_generation=GREATEST(NEW.content_generation,OLD.content_generation+1);
            SET NEW.reference_sha256='',NEW.reference_size_bytes=0;
        END IF;
    END IF;
END$$

DROP TRIGGER IF EXISTS transfer_artifact_source_revision$$
CREATE TRIGGER transfer_artifact_source_revision BEFORE UPDATE ON song_resource_artifacts FOR EACH ROW
BEGIN
    IF NOT (NEW.source_resource_id <=> OLD.source_resource_id) OR NOT (NEW.artifact_type <=> OLD.artifact_type)
       OR NOT (NEW.source_checksum <=> OLD.source_checksum) OR NOT (NEW.processor_version <=> OLD.processor_version)
       OR NOT (NEW.schema_version <=> OLD.schema_version) THEN
        SET NEW.source_revision=GREATEST(NEW.source_revision,OLD.source_revision+1);
        SET NEW.content_generation=GREATEST(NEW.content_generation,OLD.content_generation+1);
        SET NEW.reference_sha256='',NEW.reference_size_bytes=0;
        SET NEW.claim_generation=GREATEST(NEW.claim_generation,OLD.claim_generation+1),NEW.running_batch_id='';
        IF NEW.status=1 THEN SET NEW.status=IF(OLD.local_content_generation>0,2,0); END IF;
    END IF;
END$$

DROP TRIGGER IF EXISTS transfer_song_resource_revision$$
CREATE TRIGGER transfer_song_resource_revision BEFORE UPDATE ON song_resources FOR EACH ROW
BEGIN
    IF NOT (NEW.song_source_ref_id <=> OLD.song_source_ref_id) OR NOT (NEW.resource_kind <=> OLD.resource_kind) OR NOT (NEW.format_type <=> OLD.format_type) THEN
        SET NEW.source_revision=GREATEST(NEW.source_revision,OLD.source_revision+1);
        SET NEW.content_generation=GREATEST(NEW.content_generation,OLD.content_generation+1);
        SET NEW.reference_sha256='',NEW.reference_size_bytes=0;
        SET NEW.claim_generation=GREATEST(NEW.claim_generation,OLD.claim_generation+1),NEW.running_batch_id=NULL;
        IF NEW.status=1 THEN SET NEW.status=IF(OLD.local_content_generation>0,2,0); END IF;
    END IF;
END$$

DROP TRIGGER IF EXISTS transfer_song_source_revision$$
CREATE TRIGGER transfer_song_source_revision AFTER UPDATE ON song_source_refs FOR EACH ROW
BEGIN
    IF NOT (NEW.source <=> OLD.source) OR NOT (NEW.source_song_id <=> OLD.source_song_id) OR NOT (NEW.source_content_id <=> OLD.source_content_id)
       OR NOT (NEW.source_resource_type <=> OLD.source_resource_type) OR NOT (NEW.copyright_id <=> OLD.copyright_id) THEN
        UPDATE song_resources SET source_revision=source_revision+1,claim_generation=claim_generation+1,running_batch_id=NULL,status=IF(status=1,IF(local_content_generation>0,2,0),status) WHERE song_source_ref_id=NEW.id;
    END IF;
END$$

DROP TRIGGER IF EXISTS transfer_song_format_insert$$
CREATE TRIGGER transfer_song_format_insert AFTER INSERT ON song_source_audio_formats FOR EACH ROW
BEGIN
    UPDATE song_resources SET source_revision=source_revision+1,claim_generation=claim_generation+1,running_batch_id=NULL,status=IF(status=1,IF(local_content_generation>0,2,0),status) WHERE song_source_ref_id=NEW.song_source_ref_id;
END$$

DROP TRIGGER IF EXISTS transfer_song_format_update$$
CREATE TRIGGER transfer_song_format_update AFTER UPDATE ON song_source_audio_formats FOR EACH ROW
BEGIN
    IF NOT (NEW.song_source_ref_id <=> OLD.song_source_ref_id) OR NOT (NEW.format_type <=> OLD.format_type) OR NOT (NEW.resource_type <=> OLD.resource_type)
       OR NOT (NEW.iformat <=> OLD.iformat) OR NOT (NEW.aformat <=> OLD.aformat) THEN
        UPDATE song_resources SET source_revision=source_revision+1,claim_generation=claim_generation+1,running_batch_id=NULL,status=IF(status=1,IF(local_content_generation>0,2,0),status) WHERE song_source_ref_id=OLD.song_source_ref_id;
        IF NEW.song_source_ref_id<>OLD.song_source_ref_id THEN UPDATE song_resources SET source_revision=source_revision+1,claim_generation=claim_generation+1,running_batch_id=NULL,status=IF(status=1,IF(local_content_generation>0,2,0),status) WHERE song_source_ref_id=NEW.song_source_ref_id; END IF;
    END IF;
END$$

DROP TRIGGER IF EXISTS transfer_song_format_delete$$
CREATE TRIGGER transfer_song_format_delete AFTER DELETE ON song_source_audio_formats FOR EACH ROW
BEGIN
    UPDATE song_resources SET source_revision=source_revision+1,claim_generation=claim_generation+1,running_batch_id=NULL,status=IF(status=1,IF(local_content_generation>0,2,0),status) WHERE song_source_ref_id=OLD.song_source_ref_id;
END$$

DELIMITER ;
