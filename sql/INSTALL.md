# Installation de pg_cron sur Debian / PostgreSQL 18

## 1. Installer le paquet

```bash
sudo apt install postgresql-18-cron
```

## 2. Configurer PostgreSQL

Créer ou éditer `/etc/postgresql/18/main/conf.d/kaza.conf` et y ajouter :

```ini
shared_preload_libraries = 'pg_cron'
cron.database_name = 'kaza'
```

## 3. Redémarrer PostgreSQL

```bash
sudo systemctl restart postgresql
```

## 4. Activer l'extension dans la base

Se connecter à la base `kaza` et exécuter :

```sql
CREATE EXTENSION pg_cron;
```

## 5. Vérifier l'installation

```sql
-- Doit retourner une table vide sans erreur
SELECT * FROM cron.job;
```
