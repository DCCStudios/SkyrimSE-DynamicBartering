-- CHIM-DynamicBartering :: initial schema
-- Records every barter outcome pushed by the DynamicBartering SKSE plugin.
-- Used for the immediate-reaction cooldown and for history/analytics. PostgreSQL.

CREATE TABLE IF NOT EXISTS barter_events (
    rowid         BIGSERIAL PRIMARY KEY,
    localts       BIGINT  NOT NULL,
    merchant      TEXT,
    merchant_id   TEXT,
    personality   TEXT,
    relationship  INTEGER,
    action        TEXT,
    item          TEXT,
    market_price  INTEGER,
    offered_price INTEGER,
    gold_delta    INTEGER,
    is_buying     BOOLEAN,
    is_stolen     BOOLEAN,
    big_moment    BOOLEAN,
    narrative     TEXT
);

CREATE INDEX IF NOT EXISTS idx_barter_events_merchant ON barter_events (merchant);
CREATE INDEX IF NOT EXISTS idx_barter_events_localts  ON barter_events (localts);
CREATE INDEX IF NOT EXISTS idx_barter_events_big      ON barter_events (big_moment, merchant);
