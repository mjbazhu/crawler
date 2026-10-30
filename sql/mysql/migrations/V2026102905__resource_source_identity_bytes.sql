-- 按实际字节比较来源身份；默认不区分大小写的表排序规则不能代替 URL/来源身份比较。
DELIMITER $$
DROP TRIGGER IF EXISTS transfer_singer_source_revision$$
CREATE TRIGGER transfer_singer_source_revision BEFORE UPDATE ON singer_resources FOR EACH ROW
BEGIN
    IF NOT (NEW.singer_source_ref_id <=> OLD.singer_source_ref_id) OR NOT (CAST(NEW.resource_kind AS BINARY) <=> CAST(OLD.resource_kind AS BINARY)) OR NOT (CAST(NEW.remote_url AS BINARY) <=> CAST(OLD.remote_url AS BINARY)) THEN
        SET NEW.source_revision=GREATEST(NEW.source_revision,OLD.source_revision+1);
        SET NEW.claim_generation=GREATEST(NEW.claim_generation,OLD.claim_generation+1),NEW.running_batch_id=NULL;
        IF NEW.status=1 THEN SET NEW.status=IF(OLD.local_content_generation>0,2,0); END IF;
        SET NEW.cloud_claim_generation=GREATEST(NEW.cloud_claim_generation,OLD.cloud_claim_generation+1),NEW.cloud_running_batch_id=NULL;
        IF NEW.cloud_status=1 THEN SET NEW.cloud_status=4; END IF;
        IF NOT (NEW.singer_source_ref_id <=> OLD.singer_source_ref_id) OR NOT (CAST(NEW.resource_kind AS BINARY) <=> CAST(OLD.resource_kind AS BINARY)) THEN
            SET NEW.content_generation=GREATEST(NEW.content_generation,OLD.content_generation+1);
            SET NEW.reference_sha256='',NEW.reference_size_bytes=0;
        END IF;
    END IF;
END$$

DROP TRIGGER IF EXISTS transfer_album_source_revision$$
CREATE TRIGGER transfer_album_source_revision BEFORE UPDATE ON album_resources FOR EACH ROW
BEGIN
    IF NOT (NEW.album_source_ref_id <=> OLD.album_source_ref_id) OR NOT (CAST(NEW.resource_kind AS BINARY) <=> CAST(OLD.resource_kind AS BINARY)) OR NOT (CAST(NEW.remote_url AS BINARY) <=> CAST(OLD.remote_url AS BINARY)) THEN
        SET NEW.source_revision=GREATEST(NEW.source_revision,OLD.source_revision+1);
        SET NEW.claim_generation=GREATEST(NEW.claim_generation,OLD.claim_generation+1),NEW.running_batch_id=NULL;
        IF NEW.status=1 THEN SET NEW.status=IF(OLD.local_content_generation>0,2,0); END IF;
        SET NEW.cloud_claim_generation=GREATEST(NEW.cloud_claim_generation,OLD.cloud_claim_generation+1),NEW.cloud_running_batch_id=NULL;
        IF NEW.cloud_status=1 THEN SET NEW.cloud_status=4; END IF;
        IF NOT (NEW.album_source_ref_id <=> OLD.album_source_ref_id) OR NOT (CAST(NEW.resource_kind AS BINARY) <=> CAST(OLD.resource_kind AS BINARY)) THEN
            SET NEW.content_generation=GREATEST(NEW.content_generation,OLD.content_generation+1);
            SET NEW.reference_sha256='',NEW.reference_size_bytes=0;
        END IF;
    END IF;
END$$

DROP TRIGGER IF EXISTS transfer_artifact_source_revision$$
CREATE TRIGGER transfer_artifact_source_revision BEFORE UPDATE ON song_resource_artifacts FOR EACH ROW
BEGIN
    IF NOT (NEW.source_resource_id <=> OLD.source_resource_id) OR NOT (CAST(NEW.artifact_type AS BINARY) <=> CAST(OLD.artifact_type AS BINARY))
       OR NOT (CAST(NEW.source_checksum AS BINARY) <=> CAST(OLD.source_checksum AS BINARY)) OR NOT (CAST(NEW.processor_version AS BINARY) <=> CAST(OLD.processor_version AS BINARY))
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
    IF NOT (NEW.song_source_ref_id <=> OLD.song_source_ref_id) OR NOT (CAST(NEW.resource_kind AS BINARY) <=> CAST(OLD.resource_kind AS BINARY)) OR NOT (CAST(NEW.format_type AS BINARY) <=> CAST(OLD.format_type AS BINARY)) THEN
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
    IF NOT (CAST(NEW.source AS BINARY) <=> CAST(OLD.source AS BINARY)) OR NOT (CAST(NEW.source_song_id AS BINARY) <=> CAST(OLD.source_song_id AS BINARY)) OR NOT (CAST(NEW.source_content_id AS BINARY) <=> CAST(OLD.source_content_id AS BINARY))
       OR NOT (CAST(NEW.source_resource_type AS BINARY) <=> CAST(OLD.source_resource_type AS BINARY)) OR NOT (CAST(NEW.copyright_id AS BINARY) <=> CAST(OLD.copyright_id AS BINARY)) THEN
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
    IF NOT (NEW.song_source_ref_id <=> OLD.song_source_ref_id) OR NOT (CAST(NEW.format_type AS BINARY) <=> CAST(OLD.format_type AS BINARY)) OR NOT (NEW.resource_type <=> OLD.resource_type)
       OR NOT (CAST(NEW.iformat AS BINARY) <=> CAST(OLD.iformat AS BINARY)) OR NOT (CAST(NEW.aformat AS BINARY) <=> CAST(OLD.aformat AS BINARY)) THEN
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
