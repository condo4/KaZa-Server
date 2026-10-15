-- ============================================================
-- KaZa - Infrastructure niveaux (cuve, batterie, ...)
--
-- Même schéma et politique de rétention que sql/temperature.sql,
-- pour toute mesure de type "niveau" partageant object_id/kaza_objects :
-- niveau de cuve d'eau (tank.rainwater), état de charge batterie
-- (victron.battery.soc), etc.
-- ============================================================

-- Registre des objets KaZa (partagé avec les autres domaines)
CREATE TABLE IF NOT EXISTS kaza_objects (
    rowid INTEGER      PRIMARY KEY,
    name  VARCHAR(255) NOT NULL UNIQUE
);


-- Données brutes (conservées 7 jours)
CREATE TABLE tanklevel_raw (
    object_id INTEGER   NOT NULL REFERENCES kaza_objects(rowid),
    ts        TIMESTAMP NOT NULL,
    value     REAL      NOT NULL,
    PRIMARY KEY (object_id, ts)
);

-- Index sur ts seul pour les requêtes de rollup (WHERE ts < ...)
CREATE INDEX tanklevel_raw_ts_idx ON tanklevel_raw (ts);


-- Agrégats horaires (conservés 1 mois)
CREATE TABLE tanklevel_hourly (
    object_id INTEGER   NOT NULL REFERENCES kaza_objects(rowid),
    ts        TIMESTAMP NOT NULL,
    min       REAL      NOT NULL,
    max       REAL      NOT NULL,
    avg       REAL      NOT NULL,
    count     INTEGER   NOT NULL,
    PRIMARY KEY (object_id, ts)
);

CREATE INDEX tanklevel_hourly_ts_idx ON tanklevel_hourly (ts);


-- Agrégats journaliers (conservés indéfiniment)
CREATE TABLE tanklevel_daily (
    object_id INTEGER NOT NULL REFERENCES kaza_objects(rowid),
    ts        DATE    NOT NULL,
    min       REAL    NOT NULL,
    max       REAL    NOT NULL,
    avg       REAL    NOT NULL,
    count     INTEGER NOT NULL,
    PRIMARY KEY (object_id, ts)
);


-- ============================================================
-- Procédure : rollup raw -> hourly + purge raw > 7 jours
-- Déclenchée toutes les heures par pg_cron
-- On ne traite que les heures complètes (< date_trunc heure courante)
-- ============================================================
CREATE OR REPLACE PROCEDURE rollup_tanklevel_hourly()
LANGUAGE plpgsql AS $$
BEGIN
    INSERT INTO tanklevel_hourly (object_id, ts, min, max, avg, count)
    SELECT
        object_id,
        date_trunc('hour', ts),
        MIN(value),
        MAX(value),
        AVG(value),
        COUNT(*)
    FROM tanklevel_raw
    WHERE ts < date_trunc('hour', NOW()) - INTERVAL '7 days'
    GROUP BY object_id, date_trunc('hour', ts)
    ON CONFLICT (object_id, ts) DO UPDATE SET
        min   = EXCLUDED.min,
        max   = EXCLUDED.max,
        avg   = EXCLUDED.avg,
        count = EXCLUDED.count;

    DELETE FROM tanklevel_raw
    WHERE ts < date_trunc('hour', NOW()) - INTERVAL '7 days';
END;
$$;


-- ============================================================
-- Procédure : rollup hourly -> daily + purge hourly > 1 mois
-- Déclenchée chaque jour à 02:05 par pg_cron
-- Moyenne pondérée : SUM(avg * count) / SUM(count)
-- ============================================================
CREATE OR REPLACE PROCEDURE rollup_tanklevel_daily()
LANGUAGE plpgsql AS $$
BEGIN
    INSERT INTO tanklevel_daily (object_id, ts, min, max, avg, count)
    SELECT
        object_id,
        date_trunc('day', ts)::DATE,
        MIN(min),
        MAX(max),
        SUM(avg * count) / SUM(count),
        SUM(count)
    FROM tanklevel_hourly
    WHERE ts < date_trunc('day', NOW()) - INTERVAL '1 month'
    GROUP BY object_id, date_trunc('day', ts)::DATE
    ON CONFLICT (object_id, ts) DO UPDATE SET
        min   = EXCLUDED.min,
        max   = EXCLUDED.max,
        avg   = EXCLUDED.avg,
        count = EXCLUDED.count;

    DELETE FROM tanklevel_hourly
    WHERE ts < date_trunc('day', NOW()) - INTERVAL '1 month';
END;
$$;


-- ============================================================
-- Planification pg_cron
-- ============================================================

-- Rollup horaire : toutes les heures à H:05
SELECT cron.schedule(
    'rollup-tanklevel-hourly',
    '5 * * * *',
    'CALL rollup_tanklevel_hourly()'
);

-- Rollup journalier : chaque jour à 02:05
SELECT cron.schedule(
    'rollup-tanklevel-daily',
    '5 2 * * *',
    'CALL rollup_tanklevel_daily()'
);
