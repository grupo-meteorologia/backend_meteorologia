CREATE TABLE IF NOT EXISTS weather (
    lat REAL NOT NULL,
    lon REAL NOT NULL,
    variable TEXT NOT NULL,
    value REAL NOT NULL,
    timestamp TEXT NOT NULL,
    PRIMARY KEY (lat, lon, variable, timestamp)
);

CREATE INDEX IF NOT EXISTS idx_weather_lookup
    ON weather (lat, lon, variable);

CREATE TABLE IF NOT EXISTS missing_requests (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    lat REAL NOT NULL,
    lon REAL NOT NULL,
    timestamp TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    ready INTEGER NOT NULL DEFAULT 0
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_missing_unique
    ON missing_requests (lat, lon);

