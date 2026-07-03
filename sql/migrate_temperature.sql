-- ============================================================
-- Migration températures : ancienne structure → nouvelle
--
-- Anciens IDs :
--   13 → meas.temperature.parking  (nouveau rowid 11267)
--   14 → meas.temperature.porch    (nouveau rowid 11264)
--   54 → device.pac.temperature    (nouveau rowid 6422)
-- ============================================================

-- 1. Enregistrement des objets
INSERT INTO kaza_objects (rowid, name) VALUES
    (11267, 'meas.temperature.parking'),
    (11264, 'meas.temperature.porch'),
    (6422,  'device.pac.temperature');


-- Correspondance ancien id → nouveau rowid
-- Utilisée comme sous-table dans les requêtes suivantes :
--   (13, 11267), (14, 11264), (54, 6422)


-- 2. Données brutes : les 7 derniers jours → temperature_raw
INSERT INTO temperature_raw (object_id, ts, value)
SELECT
    m.new_id,
    t.time_value,
    d.value
FROM public."Datapoint" d
JOIN public."Timestamp" t  ON t.id = d."timestamp"
JOIN (VALUES (13, 11267), (14, 11264), (54, 6422)) AS m(old_id, new_id)
     ON d.object = m.old_id
WHERE t.time_value >= NOW() - INTERVAL '7 days';


-- 3. Agrégats horaires : entre 7 jours et 1 mois → temperature_hourly
INSERT INTO temperature_hourly (object_id, ts, min, max, avg, count)
SELECT
    m.new_id,
    date_trunc('hour', t.time_value),
    MIN(d.value),
    MAX(d.value),
    AVG(d.value),
    COUNT(*)
FROM public."Datapoint" d
JOIN public."Timestamp" t  ON t.id = d."timestamp"
JOIN (VALUES (13, 11267), (14, 11264), (54, 6422)) AS m(old_id, new_id)
     ON d.object = m.old_id
WHERE t.time_value >= NOW() - INTERVAL '1 month'
  AND t.time_value <  NOW() - INTERVAL '7 days'
GROUP BY m.new_id, date_trunc('hour', t.time_value);


-- 4. Agrégats journaliers : plus d'un mois → temperature_daily
INSERT INTO temperature_daily (object_id, ts, min, max, avg, count)
SELECT
    m.new_id,
    date_trunc('day', t.time_value)::DATE,
    MIN(d.value),
    MAX(d.value),
    AVG(d.value),
    COUNT(*)
FROM public."Datapoint" d
JOIN public."Timestamp" t  ON t.id = d."timestamp"
JOIN (VALUES (13, 11267), (14, 11264), (54, 6422)) AS m(old_id, new_id)
     ON d.object = m.old_id
WHERE t.time_value < NOW() - INTERVAL '1 month'
GROUP BY m.new_id, date_trunc('day', t.time_value)::DATE;
