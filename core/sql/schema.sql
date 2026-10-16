-- Klend-ario schema.
--
-- Additive and versioned: migrations run on open and the current version lives
-- in `meta.schema_version`. Everything here is IF NOT EXISTS, so applying it is
-- safe on every open and there is never a destructive statement in this file.
--
-- This is the ONE copy of the schema: it is embedded into the binary at
-- configure time (see core/CMakeLists.txt) rather than restated in C++.

CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

-- One row per event. Individual rows (not blobs) because the MCP server must
-- be able to query them, and because a weekly view is a range query over
-- start_utc/end_utc.
CREATE TABLE IF NOT EXISTS events (
    id           INTEGER PRIMARY KEY,
    title        TEXT    NOT NULL,
    start_utc    INTEGER NOT NULL,          -- Unix seconds, UTC
    end_utc      INTEGER NOT NULL,
    type         TEXT    NOT NULL DEFAULT 'event'
                 CHECK (type IN ('event', 'sleep', 'wake', 'alarm')),
    importance   INTEGER NOT NULL DEFAULT 50
                 CHECK (importance BETWEEN 0 AND 100),   -- 0..100 -> hue
    urgency      INTEGER NOT NULL DEFAULT 50
                 CHECK (urgency BETWEEN 0 AND 100),      -- 0..100 -> border
    alarm_offset INTEGER NOT NULL DEFAULT 0,             -- seconds before start
    hidden       INTEGER NOT NULL DEFAULT 0,             -- 1 = hidden in view
    notes        TEXT,
    created_utc  INTEGER NOT NULL,
    updated_utc  INTEGER NOT NULL,
    CHECK (end_utc >= start_utc)
);

CREATE INDEX IF NOT EXISTS idx_events_range ON events(start_utc, end_utc);
CREATE INDEX IF NOT EXISTS idx_events_type  ON events(type);
