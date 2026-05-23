CREATE TABLE IF NOT EXISTS songs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT NOT NULL,
    song_id INTEGER,
    artist TEXT,
    singer_id INTEGER,
    album TEXT,
    album_id INTEGER,
    artwork TEXT,
    lrc TEXT,
    created_at INTEGER,
    updated_at INTEGER,
    UNIQUE(song_id)
);

CREATE TABLE IF NOT EXISTS singers (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    singer_id INTEGER,
    artwork TEXT,
    genre TEXT,
    country TEXT,
    gender INTEGER,
    created_at INTEGER,
    updated_at INTEGER,
    status INTEGER NOT NULL DEFAULT 0,
    UNIQUE(singer_id)
);

CREATE TABLE IF NOT EXISTS albums (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT NOT NULL,
    album_id INTEGER,
    artwork TEXT,
    artist TEXT,
    singer_id INTEGER,
    album_infos TEXT,
    listen_number INTEGER,
    created_at INTEGER,
    updated_at INTEGER,
    status INTEGER NOT NULL DEFAULT 0,
    UNIQUE(album_id)
);

CREATE INDEX IF NOT EXISTS idx_songs_title ON songs(title);
CREATE INDEX IF NOT EXISTS idx_songs_artist ON songs(artist);
CREATE INDEX IF NOT EXISTS idx_singers_name ON singers(name);
