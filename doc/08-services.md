# 08 — Services : requêtes génériques client → serveur (spécification)

Statut : **en cours d'implémentation** (phases 0 à 4 faites, reste 5 et 6 : voir §12 « Suivi d'avancement »). Remplace à terme `FRAME_DBQUERY`/`FRAME_DBRESULT` et généralise `FRAME_HISTORY`.
Le code fait foi ; ce document décrit la cible, pas l'existant.

## 1. Contexte et problème

| Mécanisme actuel | Défaut |
|---|---|
| `FRAME_DBQUERY` (3/4) | Le client envoie du SQL arbitraire, exécuté avec les droits de `kazad` (`kazaconnection.cpp::_processFrameDbQuery`). Tout client mTLS peut lire **et écrire** la base. Pas de réponse en cas d'échec. Exécution synchrone. |
| `FRAME_HISTORY` (9/10) | Sûr, mais rigide : liste blanche de domaines (`historydomains.h`), schéma imposé `<domaine>_raw/_hourly/_daily` par `object_id`, résultat figé min/avg/max. Le message d'erreur SQL brut est renvoyé au client. |
| `KaZaManager::runDbQuery(QString)` | Prend une chaîne SQL déjà construite : pas de paramètres liés, donc injection possible côté QML serveur. |

Exemple de besoin non couvert : la table `ecowitt` (une colonne par grandeur) ne rentre pas dans `FRAME_HISTORY`.

## 2. Objectifs et non-objectifs

**Objectifs**
- Le client n'envoie **jamais** de SQL : il appelle un *service nommé* avec des paramètres clé/valeur.
- Les services sont **déclarés dans le QML serveur** (pas de recompilation, cohérent avec la philosophie KaZa).
- Le protocole est générique : un nouveau service ne demande aucune modification du protocole, de kazad ni du client.
- Sécurité par défaut : paramètres typés et validés, valeurs toujours liées (`bindValue`), lecture seule, bornes de temps/volume, ACL.
- Migration progressive sans rupture pour les APK et .deb déjà installés.

**Non-objectifs (v1)**
- Pas d'abonnement/push sur changement de donnée (mécanisme distinct, à spécifier plus tard).
- Pas de SQL libre côté client, même « restreint ».
- Pas de transactions multi-requêtes côté client.

## 3. Vue d'ensemble

```
Client QML                       kazad                                   QML serveur
KzRequest/KzTable  --REQUEST-->  KaZaConnection                          KzService "ecowitt.history"
 service + params                 ├ session valide ? ACL ? limites ?       ├ sql: + params typés   (déclaratif)
                                  ├ validation des params (types/bornes)   └ onRequest(req)        (scriptable)
                  <--RESPONSE--   ├ exécution asynchrone (pool SQL RO)
 colonnes+lignes / valeur         └ statut + code d'erreur (jamais de SQL)
```

## 4. Protocole

### 4.1 Nouvelles trames

Format inchangé : `[type u8][len u32 BE][payload]`. Les nouveaux types s'ajoutent à l'enum après `FRAME_HISTORYRESULT` :

| Type | Nom | Sens |
|---|---|---|
| 11 | `FRAME_REQUEST` | C→S |
| 12 | `FRAME_RESPONSE` | S→C |
| 13 | `FRAME_CANCEL` | C→S (annule une requête en cours) |

> Les valeurs 9/10 restent réservées à `FRAME_HISTORY*` pendant la transition. Toutes les copies du codec sont à modifier ensemble (cf. §10).

Tous les payloads sont des `QDataStream` `Qt_6_0`, comme les trames existantes.

**`FRAME_REQUEST`**
```
quint32      queryId        choisi par le client, unique parmi ses requêtes en vol
QString      service        ex. "ecowitt.history"
QVariantMap  params         clés/valeurs génériques
```

**`FRAME_RESPONSE`**
```
quint32      queryId
quint8       status         voir §4.3
QString      errorCode      vide si status == OK ; code court, sans détail interne
QString      errorMessage   message lisible et sûr (jamais de SQL, de nom de table ni de chemin)
quint8       kind           0 = vide, 1 = table, 2 = valeur
bool         truncated      true si maxRows atteint
payload selon kind :
   table  : QStringList columns, QList<QList<QVariant>> rows
   valeur : QVariant
```
Le `payload` est écrit comme un `QByteArray` contenant le flux `QDataStream` (`Qt_6_0`) des colonnes+lignes ou de la valeur. S'il dépasse 4096 octets, il est `qCompress`é (même convention qu'`OBJLIST`) et le bit de compression est porté par `kind` (`kind | 0x80`). À la réception, la taille annoncée par `qCompress` est vérifiée avant décompression (16 Mio max) ; une requête de plus de 64 Kio ou de plus de 64 clés, ou toute trame mal formée, est ignorée sans fermer la connexion.

**`FRAME_CANCEL`** : `quint32 queryId`. Une requête encore en file est retirée ; une requête déjà en cours d'exécution n'est **pas** interrompue côté PostgreSQL (elle est bornée par `statement_timeout`), son résultat est simplement abandonné. Le serveur ne renvoie **aucune** réponse. Une réponse déjà en route est ignorée par le client.

### 4.2 Règles de session
- Comme pour toutes les trames : `REQUEST`/`CANCEL` avant `VERSION` valide ⇒ déconnexion.
- Une trame `REQUEST` dont le `queryId` est déjà en vol sur la même connexion est rejetée (`status = BAD_REQUEST`).
- Une réponse est **toujours** envoyée pour une requête non annulée (succès ou erreur) : le client peut donc armer un timeout fiable (défaut recommandé : 30 s).
- Ordre des réponses non garanti.

### 4.3 Statuts et codes d'erreur

| status | Nom | Signification |
|---|---|---|
| 0 | `OK` | |
| 1 | `UNKNOWN_SERVICE` | service non déclaré **ou non autorisé** pour cet utilisateur (indistinguables volontairement) |
| 2 | `INVALID_PARAMS` | paramètre manquant, inconnu, mauvais type ou hors bornes (`errorCode` = nom du paramètre) |
| 3 | `TIMEOUT` | dépassement de `timeout` du service |
| 4 | `BUSY` | trop de requêtes en vol / limite de débit |
| 5 | `TOO_LARGE` | résultat refusé (taille) |
| 6 | `UNAVAILABLE` | base indisponible |
| 7 | `INTERNAL` | erreur interne ; détail dans le journal serveur uniquement |

### 4.4 Négociation de capacités
Le serveur n'incrémente pas la version mineure (règle `peerMinor<=0`). La disponibilité est annoncée par une commande SYSTEM, à la suite de `CAPS:` prévu pour les notifications :
- S→C après `CONNECTED` : `CAPS:services=1[,notify=1,…]`.
- Un client qui ne reçoit pas `services=1` doit se replier sur `FRAME_HISTORY`/`DBQUERY` (période de transition) ou désactiver la fonction.
- Un pair qui reçoit un type de trame inconnu l'ignore **sans fermer la connexion**. Vérifié : le `switch` de `KaZaProtocol::_dataReady` n'a pas de `default`, la trame est simplement consommée (`m_pending.remove(0, len+5)`). Un ancien kazad ou client ignore donc `REQUEST/RESPONSE/CANCEL` sans casser la session. Attention : une trame de longueur 0 (`len < 1`) bloque le flux ; `REQUEST` ne doit jamais être vide (le nom du service garantit `len > 4`).

### 4.5 Découverte
Commande SYSTEM `SERVICES?` (C→S) ; réponse `SERVICES:<b64 JSON>` listant, pour les services visibles par l'utilisateur : nom, description, paramètres (nom, type, requis, défaut, bornes, valeurs d'enum), `maxRows`. Sert à la documentation et aux outils de debug ; le client n'en dépend pas.

## 5. Côté serveur QML

Import : `org.kazoe.kaza 1.0`. Deux niveaux.

### 5.1 `KzService` déclaratif (cas par défaut)

```qml
KzService {
    name: "ecowitt.history"
    description: "Courbe d'une grandeur météo, min/avg/max par pas"
    sql: "SELECT time_bucket(:step::interval, time) AS t,
                 min(%metric%), avg(%metric%), max(%metric%)
          FROM ecowitt
          WHERE time >= :start AND time < :end
          GROUP BY t ORDER BY t"
    params: [
        KzParam { name: "start";  type: KzParam.DateTime; required: true },
        KzParam { name: "end";    type: KzParam.DateTime; required: true },
        KzParam { name: "step";   type: KzParam.Enum; values: ["10 minutes", "1 hour", "1 day"]; defaultValue: "1 hour" },
        KzParam { name: "metric"; type: KzParam.Identifier;       // substitué dans %metric%
                  values: ["temp", "humidity", "baromrel", "windspeed", "solarradiation"] }
    ]
    maxRows: 5000
    timeout: 10000      // ms
    users: ["*"]        // ou ["alice", "bob"]
}
```

Règles :
- `:nom` = valeur **liée** (`QSqlQuery::bindValue`), jamais concaténée.
- `%nom%` = substitution d'**identifiant**, uniquement pour un `KzParam` de type `Identifier` (liste `values` fermée, chaque valeur étant un identifiant SQL simple) ; la valeur doit appartenir à la liste. Un `Enum` est toujours **lié** (`:nom`), jamais substitué. Un `%nom%` qui apparaît aussi dans une chaîne entre apostrophes est refusé (ambiguïté).
- Un paramètre reçu et non déclaré ⇒ `INVALID_PARAMS`. Un placeholder `:x` sans `KzParam` correspondant est refusé **au chargement** du QML (erreur au démarrage, pas à l'exécution).
- Types de `KzParam` : `Int`, `Double`, `Bool`, `String` (`maxLength`), `DateTime` (UTC), `Enum`, `Identifier`. Attributs : `required`, `defaultValue`, `min`, `max`, `values`, `maxLength`.
- Une seule instruction SQL par service, qui doit commencer par `SELECT` ou `WITH`. Le moteur refuse `;` interne, les commentaires (`--`, `/* */`), `$` et `?`. Le contrôle est fait au chargement du QML : un service non conforme n'est pas enregistré (erreur dans le journal) et les requêtes qui le visent reçoivent `UNKNOWN_SERVICE`.
- Un `WITH` peut contenir une écriture (`WITH x AS (INSERT …)`) : c'est la transaction `READ ONLY` de PostgreSQL qui la refuse, pas l'analyse du texte.
- `kind` de la réponse : table par défaut ; `single: true` ⇒ première cellule en valeur.

### 5.2 `KzService` scriptable (cas complexes)

```qml
KzService {
    name: "energy.summary"
    params: [ KzParam { name: "since"; type: KzParam.DateTime; required: true } ]
    users: ["*"]
    handler: function(req) {                          // req.params : déjà validés
        kazamanager.query("SELECT sum(kwh) FROM energy WHERE day >= :d", { d: req.params.since },
            function(r) {
                if (!r.ok) { req.fail("db"); return }
                req.respondValue(r.rows[0][0])
            })
    }
}
```
- Le handler est une **propriété** `handler` (et non un signal `onRequest`) : l'appel d'une fonction JS permet de détecter l'exception et de répondre `INTERNAL` tout de suite plutôt qu'au timeout. `sql` et `handler` sont exclusifs.
- Le handler s'exécute dans le thread principal et ne doit **jamais bloquer**. L'accès base se fait par `kazamanager.query(sql, bindings, callback)` : asynchrone, exécuté sur le pool de service en transaction `READ ONLY`, mêmes règles de texte que le SQL déclaratif (une instruction `SELECT`/`WITH`, pas de commentaire, tout `:nom` doit avoir sa valeur liée, aucun `%nom%`), 1000 lignes max. Le callback reçoit `{ ok, status, errorCode, columns, rows, truncated }`.
- Réponses : `req.respondTable(columns, rows)`, `req.respondValue(v)`, `req.respondEmpty()`, `req.fail("code")` (`INTERNAL` avec ce code s'il vaut `[a-z0-9_]{1,32}`, sinon `internal`), `req.invalid("param", "message")` (`INVALID_PARAMS`). Seule la première réponse compte ; la réponse peut venir plus tard. Sans réponse avant `timeout`, kazad répond `TIMEOUT` et les réponses tardives sont ignorées.
- Une exception JS ⇒ `INTERNAL` immédiat + journal (le message d'exception ne va jamais au client).
- `req.user` donne l'identité authentifiée (§6).

### 5.3 Services intégrés
`kaza.history` : réimplémentation de `FRAME_HISTORY` (mêmes domaines blancs, mêmes tables) derrière le nouveau mécanisme, pour que `KzEvolution` migre sans changer de comportement. Les paramètres reprennent `domain, object, start, end, step`.

## 6. Sécurité

1. **Pas de SQL client** : seul un nom de service et des valeurs typées traversent le réseau.
2. **Transaction `READ ONLY`** sur une **connexion séparée** pour les services (rôle PostgreSQL existant, cf. §11.1). L'écriture (`logMeasurement`, `runDbQuery`) garde sa connexion actuelle. Les services d'écriture sont hors v1 (§11.4).
3. **Bornes** : `statement_timeout` positionné par session de service, `maxRows`, taille maximale de `REQUEST` (ex. 64 Ko) et de `params` (nombre de clés), `maxInFlight` par connexion (défaut 8) ⇒ `BUSY`.
4. **Authentification/ACL** : `users` filtré sur l'identité de la connexion. Prérequis : lier l'identité `user` du `VERSION` au **CN du certificat client** (aujourd'hui l'identité est déclarative, l'ACL serait contournable). Tant que ce prérequis n'est pas fait, documenter que `users` n'est pas une barrière de sécurité forte.
5. **Pas de fuite** : réponses d'erreur sans SQL, nom de table ni chemin ; détail dans le journal serveur avec `queryId` et utilisateur.
6. **Journalisation** : service, utilisateur, durée, nombre de lignes, statut (jamais les valeurs de paramètres sensibles si un service les marque `secret: true`).
7. **Hors thread principal** : exécution dans un pool de connexions SQL ; aucun appel SQL bloquant dans le thread des connexions réseau.

## 7. Côté client

### 7.1 Types QML (`org.kazoe.kaza 1.0`)

```qml
KzRequest {                       // une requête, résultat brut
    id: req
    service: "ecowitt.history"
    params: ({ start: range.from, end: range.to, step: "1 hour", metric: "temp" })
    autoSend: true                // renvoie quand params/service changent ; annule la requête précédente
    // lecture seule : state (Idle|Loading|Ready|Error), status, errorCode, errorMessage,
    //                 columns, rows, value, truncated ; signaux : finished(), failed()
}

KzTable {                         // QAbstractTableModel au-dessus d'un KzRequest, pour ChartView/TableView
    request: req
}
```

- `KzHistory` conserve son API (`query`) pendant la transition, marquée dépréciée ; il gagne une propriété `service`/`params` pour migrer écran par écran.
- Timeout client 30 s ⇒ `state = Error`, `errorCode = "timeout"`.
- Pas de relance automatique sur erreur serveur (évite les boucles) ; relance sur reconnexion pour les requêtes `autoSend`.

### 7.1 bis État actuel du client (`KaZa-Client/src`)
- `DbQuery` (`dbquery.{h,cpp}`) : id incrémental statique + table `m_pending`, résultat porté par `KaZaProtocol::frameDbQueryResult`. **Bug à corriger au passage** : le slot `DbQuery::_dbResult` prend `uint8_t id` alors que le signal émet `uint32_t` ; au-delà de 255 requêtes, les résultats ne correspondent plus. `KzRequest` doit porter l'id en `uint32_t`.
- `KzHistory` (`kzhistory.cpp`) s'appuie sur `DbQuery` ; `KzEvolution` (`kzevolution.cpp`) sur `FRAME_HISTORY`. Les deux sont enregistrés dans `main.cpp` (lignes `qmlRegisterType`) ; `KzRequest`/`KzTable` s'y ajoutent de la même façon.
- `databaseworker.{h,cpp}` est un fichier vide : candidat à suppression ou à réutiliser pour la file de requêtes.
- Le codec `KaZa-Client/protocol/kazaprotocol.cpp` est **identique** à `KaZa-Server/protocol/` (vérifié par `diff`) ; la copie `KaZa-Builder` reste à resynchroniser.

### 7.2 Android
Le service Java n'utilise pas ces trames. `KaZaProtocol.java` reçoit uniquement les constantes pour rester aligné avec les autres copies.

## 8. Application aux services existants

| Besoin | Service | Remarque |
|---|---|---|
| Courbes Ecowitt | `ecowitt.history` (§5.1) | table `ecowitt`, pas = enum |
| Synthèse journalière / pluie | `ecowitt.daily` | table `ecowitt_daily` |
| Température / niveau de cuve | `kaza.history` (intégré) | remplace `FRAME_HISTORY` |
| Écrans actuels `KzHistory` avec SQL (`HistoryMinAvgMax.qml`, `HistoryEvo.qml`) | un service chacun | supprime le SQL de `app.rcc` |

## 9. Compatibilité et migration

| Étape | `DBQUERY` | `HISTORY` | `SERVICES` |
|---|---|---|---|
| A. Introduction | actif | actif | actif |
| B. Migration des écrans | actif, journalise un avertissement par usage | actif | actif |
| C. Durcissement | **désactivé par défaut** (`[security] allowDbQuery=false`) | réimplémenté sur `kaza.history` | actif |
| D. Retrait | supprimé (types 3/4 réservés, ignorés) | supprimé | actif |

Les clients anciens continuent de fonctionner jusqu'à l'étape C ; au passage en C, ils perdent seulement les graphes (message d'erreur explicite côté client à prévoir).

## 10. Plan d'exécution

Chaque phase est livrable et testable seule. Tout changement de protocole touche **toutes les copies** : `KaZa-Server/protocol/`, `KaZa-Client/protocol/` (sous-module), `KaZa-Builder/KaZa-Protocol/` (en retard de 3 commits : à resynchroniser d'abord), `KaZaProtocol.java`, et `/PROTOCOL.md`.

### Phase 0 — Préparation (0,5 j)
- Resynchroniser `KaZa-Builder/KaZa-Protocol` avec le sous-module du client.
- Ajouter un test automatisé du comportement déjà correct de `KaZaProtocol` sur un type de trame inconnu (ignorée, connexion conservée), plus trame vide et trame fragmentée (cf. §4.4).
- Corriger `DbQuery::_dbResult(uint8_t id, …)` en `uint32_t` (cf. §7.1 bis).
- Vérifier que `ecowitt` et `ecowitt_daily` sont lisibles par le rôle de `kazad` (`GRANT SELECT` si besoin, `ecowitt.sql` n'a pas de `OWNER`) et le documenter dans `sql/INSTALL.md`.
- **Critère** : le protocole tolère les types inconnus ; le rôle de `kazad` lit les deux tables.

### Phase 1 — Codec protocole (1 j)
- Ajouter `FRAME_REQUEST/RESPONSE/CANCEL`, `sendRequest/sendResponse/sendCancel` et signaux dans `kazaprotocol.{h,cpp}` (toutes copies).
- Compression conditionnelle, limites de taille de trame/`params`.
- Tests unitaires de sérialisation aller-retour (tous `kind`, tous `QVariant` utiles, `truncated`).
- **Critère** : aller-retour validé ; le client Java compile avec les constantes.

### Phase 2 — Moteur de services dans kazad (3 j)
- Nouveau `KaZaServiceManager` : registre de services, validation des `KzParam`, ACL, limites (`maxInFlight`, `maxRows`, `timeout`), annulation.
- Pool de connexions SQL de service en transaction `READ ONLY`, `statement_timeout`, exécution dans un thread de travail ; réponse asynchrone. Test : un `INSERT`/`DROP` injecté dans un service de test est refusé par PostgreSQL.
- `KaZaConnection::_processFrameRequest/_processFrameCancel` ; codes d'erreur §4.3 ; journalisation §6.
- Annonce `CAPS:services=1`, commande `SERVICES?`.
- **Critère** : tests d'intégration avec un client de test : injection (`' OR 1=1`, `;DROP`), type invalide, paramètre inconnu, enum hors liste, timeout, `maxRows`, annulation, `BUSY`. Aucune requête n'atteint la base sans `bindValue`.

### Phase 3 — API QML serveur (2 j)
- Types `KzService`, `KzParam`, `req.respond*`, `kazamanager.query(sql, bindings)`.
- Validation au chargement (placeholders sans `KzParam`, `%x%` sans liste fermée, multi-instructions) : échec explicite au démarrage.
- Documenter dans `doc/03-server-qml.md` et `/PROTOCOL.md`.
- **Critère** : `ecowitt.history` et `ecowitt.daily` déclarés dans `KaZaTrespeyres/Server/EcoWitt.qml` et interrogeables via `openssl`/client de test.

### Phase 4 — Client (2 j)
- `KzRequest` et `KzTable` (C++ + enregistrement dans `main.cpp`) ; annulation sur changement de paramètres, timeout, état.
- Détection de `CAPS:services=1` et repli.
- Page `Client/MeteoGraph.qml` (température, pluie, vent, sélecteur de période) sur `ecowitt.history`/`ecowitt.daily`.
- Régénérer `app.rcc`.
- **Critère** : graphes Ecowitt fonctionnels sur Linux et Android, sans aucun SQL dans `app.rcc`.

### Phase 5 — Migration de l'existant (2 j)
- Service intégré `kaza.history` ; `KzEvolution` bascule dessus.
- Services pour chaque écran `KzHistory` utilisant du SQL ; avertissement serveur à chaque usage de `DBQUERY`.
- **Critère** : plus aucun écran de `KaZaTrespeyres/Client` n'émet de `DBQUERY` (vérifié par le journal).

### Phase 6 — Durcissement et retrait (1 j + délai de déploiement)
- Option `[security] allowDbQuery=false` par défaut, puis suppression des types 3/4 et de `FRAME_HISTORY` quand tous les clients sont à jour.
- Corriger la fuite d'erreur de `_processFrameHistory` si le code subsiste encore.
- Lier l'identité `user` au CN du certificat (ACL fiable).
- Mettre à jour `PROTOCOL.md`, `07-protocol.md`, `skill-kaza/references/protocol.md`.

Charge totale estimée : environ 11 j, hors délai de déploiement des clients.

## 11. Décisions
1. **Isolation SQL** : pas de rôle dédié ; les services utilisent le rôle existant, chaque requête s'exécutant dans une transaction `READ ONLY` (`BEGIN READ ONLY` / `SET TRANSACTION READ ONLY`) sur une connexion de service séparée. La protection repose donc sur kazad : le moteur ne doit jamais exposer de moyen de lever ce mode (les handlers n'ont accès qu'à `kazamanager.query`, `;` interdit, une seule instruction). Risque accepté : le rôle garde ses droits d'écriture et d'accès à toutes les tables.
2. **Pas de temps** : `Enum` de chaînes ; pas de type `Duration` en v1.
3. **Volume** : `maxRows` + `truncated` uniquement ; pas de pagination `offset/limit`.
4. **Écriture** : hors v1. Les services sont en lecture seule ; les actions restent par les objets KaZa. Le champ `readOnly: false` n'existe pas.
5. **Control Panel** : hors périmètre ; non supporté par ce chantier (déjà incompatible avec kazad 1.2.0).

## 12. Suivi d'avancement

Dernière mise à jour : 2026-10-23. Les phases 0 à 4 sont faites ; **reste la phase 5, puis la phase 6**.

### Fait

| Phase | État | Où |
|---|---|---|
| 0 Préparation | faite | tests du codec (`KaZa-Server/tests/protocol`), `DbQuery` en 32 bits, droits SQL vérifiés par l'utilisateur |
| 1 Codec | faite, poussée | sous-module `protocol` (commit `e963ac3`, puis `45231d3`), constantes dans `KaZaProtocol.java` |
| 2 Moteur kazad | faite | `src/kzparam`, `kzservice`, `kazaservicemanager`, `KaZaConnection`, `KaZaManager`, `conf/kazad.conf` `[services]` |
| 3 API QML serveur | faite | `KzService` déclaratif et `handler`, `kazamanager.query`, `KaZaTrespeyres/Server/EcoWittServices.qml`, docs `03`, `07`, `/PROTOCOL.md` |
| 4 Client | faite, commitée (`7172ab0` dans KaZa-Client) | `KzRequest`, `KzTable`, `KzServiceLink`, `KaZaTrespeyres/Client/MeteoGraph.qml`, `app.rcc` régénéré |

Tests : `KaZa-Server/tests/protocol` (18), `KaZa-Server/tests/services` (52, PostgreSQL temporaire, `KAZA_ECOWITT_SERVICES=…/EcoWittServices.qml` pour le test des services réels), `KaZa-Client/tests/services` (`tst_kzrequest` 16, `tst_meteograph` avec `KAZA_METEOGRAPH_QML=…/MeteoGraph.qml`).

Commits à faire ou faits par l'utilisateur : `KaZa-Server` (phases 0 à 3), `KaZaTrespeyres` (`EcoWittServices.qml`, `MeteoGraph.qml`, `Meteo.qml`, `KazaSidebar.qml`, `qml.qrc`, `main.qml`, `ecowitt.sql`).

### À faire : phase 5, migration de l'existant (environ 2 j)

Objectif : plus aucun écran n'envoie de SQL ni n'utilise `FRAME_HISTORY`.

1. **Service intégré `kaza.history`** dans kazad : reprend `KaZaConnection::_processFrameHistory` (domaines blancs `temperature` / `tanklevel` de `historydomains.h`, tables `<domaine>_raw/_hourly/_daily` par `object_id`, pas heure/jour/semaine/mois/année, SQL du `switch` sur `step`). Paramètres : `domain`, `object`, `start`, `end`, `step` (`Enum`). Les identifiants de table restent substitués uniquement après vérification de la liste blanche. Résultat : colonnes `ts, min, avg, max`.
2. **`KzEvolution`** (`KaZa-Client/src/kzevolution.cpp`) : envoyer `kaza.history` par `KzRequest`/`KzServiceLink` au lieu de `FRAME_HISTORY`, **sans changer son API** (propriétés `domain`, `object`, `start`, `end`, `step`, `loading`, `error`, colonnes `COL_DATE/MIN/AVG/MAX`) pour que `HistoryEvolution.qml` ne change pas. Prévoir le repli sur `FRAME_HISTORY` si le serveur n'annonce pas `CAPS:services=1`.
3. **Écrans `KzHistory` avec SQL** : `KaZaTrespeyres/Client/HistoryMinAvgMax.qml` (requête sur `time_value`) et `HistoryEvo.qml` (requête sur `logdate`) : relire leur SQL et leurs tables, le convertir en `KzService` côté serveur (dans `KaZaTrespeyres/Server/`), puis remplacer `KzHistory { query: … }` par `KzRequest` + `KzTable` (ou une variante de `KzHistory` basée sur un service). Le SQL doit disparaître de `app.rcc`. Regénérer `app.rcc`.
4. **Avertissement** dans `KaZaConnection::_processFrameDbQuery` à chaque usage de `DBQUERY` (utilisateur, début de la requête), pour repérer ce qui l'utilise encore.
5. Tests : `kaza.history` face au schéma de `sql/temperature.sql` / `tanklevel.sql` (même résultat que `FRAME_HISTORY`), `KzEvolution` migré contre un faux serveur.

Critère de réussite : le journal du serveur n'affiche plus aucun `DBQUERY` en usage normal des écrans de `KaZaTrespeyres/Client`.

### À faire : phase 6, durcissement et retrait (environ 1 j + délai de déploiement)

- Option `[security] allowDbQuery` (désactivée par défaut), puis suppression de `FRAME_DBQUERY/DBRESULT` et de `FRAME_HISTORY*` quand tous les clients sont à jour.
- Lier l'identité `user` du `VERSION` au CN du certificat client (sans cela `users` d'un `KzService` n'est pas une barrière forte).
- Corriger la fuite d'erreur de `_processFrameHistory` si le code subsiste.
- Mettre à jour `PROTOCOL.md`, `07-protocol.md`, `skill-kaza/references/protocol.md`.

### Points ouverts et réserves connues

- **Pas de test de bout en bout** client → TLS → `KaZaConnection` → moteur → PostgreSQL : les deux moitiés sont testées séparément avec le même codec. Le rendu visuel de `MeteoGraph.qml` n'a pas été vu à l'écran ni testé sur Android.
- `KaZaTrespeyres/Server/EcoWitt.qml` écrit encore en base par concaténation SQL (`kazamanager.runDbQuery("INSERT …")`) ; hors plan, à passer en requête paramétrée plus tard.
- Une requête déjà lancée sur PostgreSQL n'est pas interrompue par `FRAME_CANCEL` (bornée par `statement_timeout`).
- Une trame de longueur 0 bloque le flux du codec (défaut connu, test `XFAIL` dans `tests/protocol`).
- Face à un ancien serveur, `KzRequest` répond `unsupported` ; aucun repli automatique sur `FRAME_HISTORY` côté `KzRequest` (prévu seulement pour `KzEvolution`, point 2 de la phase 5).
- Control Panel (`KaZa-Control-Panel`) : hors périmètre.
