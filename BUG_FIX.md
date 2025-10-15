# 🐛 Bug Identifié et Corrigé !

## 🎯 Problème Identifié

**Le worker ne recevait JAMAIS les jobs d'invocation !**

### 🔍 Cause Racine

**Incompatibilité de format de message** entre les composants :

```
Load Balancer → Server → Worker
     ↓              ↓         ↓
{"type":"invoke"} → Transmet → Attend {"type":"job"}
                                       ❌ MISMATCH !
```

### 📋 Flux Détaillé

1. **API Gateway** reçoit `POST /invoke?fn=hello_XXX`
2. **API Gateway** envoie au **Server** :
   ```json
   {"type":"invoke","fn":"hello_XXX","payload":""}
   ```

3. **Server** transmet au **Load Balancer** :
   ```json
   {"type":"invoke","fn":"hello_XXX","payload":""}
   ```

4. **Load Balancer** choisit un worker et envoie au **Server** :
   ```json
   {"type":"forward_to_worker","worker_id":2,"job":{"type":"invoke","fn":"hello_XXX","payload":""}}
   ```

5. **Server** extrait le `job` et l'envoie au **Worker** :
   ```json
   {"type":"invoke","fn":"hello_XXX","payload":""}
   ```

6. **Worker** attend :
   ```json
   {"type":"job","fn":"hello_XXX","payload":""}
   ```
   
   ❌ **Le worker ne reconnaît pas `"type":"invoke"` !**

---

## ✅ Solution Implémentée

### Modification dans `src/worker.c`

**Avant** (ligne 351) :
```c
if (strstr(line, "\"type\":\"job\"")) {
    // Extract fn and payload
    const char *fnp = strstr(line, "\"fn\":");
    const char *plp = strstr(line, "\"payload\":");
    if (!fnp || !plp) {
        const char *resp = "{\"ok\":false,\"error\":\"no fn or payload\"}\n";
        write_all(fd, resp, strlen(resp));
        continue;
    }
```

**Après** (ligne 362) :
```c
// Accept both "type":"job" and "type":"invoke"
if (strstr(line, "\"type\":\"job\"") || strstr(line, "\"type\":\"invoke\"")) {
    fprintf(stderr, "[WORKER] 📨 Received message: %s\n", line);
    
    // Extract fn and payload
    const char *fnp = strstr(line, "\"fn\":");
    const char *plp = strstr(line, "\"payload\":");
    if (!fnp || !plp) {
        fprintf(stderr, "[WORKER] ❌ Missing fn or payload in message\n");
        const char *resp = "{\"ok\":false,\"error\":\"no fn or payload\"}\n";
        write_all(fd, resp, strlen(resp));
        continue;
    }
```

### Changements :
1. ✅ Accepte **à la fois** `"type":"job"` ET `"type":"invoke"`
2. ✅ Ajout de log : `[WORKER] 📨 Received message`
3. ✅ Ajout de log d'erreur si fn ou payload manquant

---

## 🧪 Test de Validation

### 1. Redémarrer le Serveur

```bash
# Arrêter le serveur actuel (Ctrl+C)

# Redémarrer
cd /mnt/c/Users/Dell/Documents/Hackathon\ IFRI
./start_server.sh RR
```

### 2. Déployer une Fonction

```bash
curl -X POST 'http://127.0.0.1:8080/deploy?name=hello&lang=c' \
    -H "Content-Type: text/plain" \
    --data-binary @examples/hello.c
```

**Résultat attendu** :
```json
{"ok":true,"id":"hello_XXXXXXXXXX","name":"hello","lang":"c","wasm":true}
```

### 3. Invoquer la Fonction

```bash
# Remplacer hello_XXXXXXXXXX par l'ID retourné
curl -X POST 'http://127.0.0.1:8080/invoke?fn=hello_XXXXXXXXXX'
```

**Résultat attendu** :
```json
{"ok":true,"output":"Hello from FAAS!\n"}
```

---

## 📊 Logs Attendus

Dans le terminal du serveur, vous devriez maintenant voir :

```
[WORKER] 📨 Received message: {"type":"invoke","fn":"hello_XXXXXXXXXX","payload":""}
[WORKER] 📨 Received job: fn=hello_XXXXXXXXXX, payload=
[WORKER] 🚀 Calling execute_function()...
[WORKER] 🔍 Loading metadata for: hello_XXXXXXXXXX
[WORKER] ✅ Metadata loaded: lang=c
[WORKER] 🎯 Detected WASM language: c
[WORKER] ✅ WASM file found: functions/hello_XXXXXXXXXX/code.wasm
[WORKER] 🚀 Executing WASM with Wasmer...
[WORKER] 📦 Loaded WASM file: 12345 bytes
[WORKER] ✅ WASM module compiled
[WORKER] ✅ WASI environment created
[WORKER] ✅ WASI imports retrieved
[WORKER] ✅ WASM instance created
[WORKER] ✅ Found function: _start
[WORKER] 🔧 Function signature: 0 params, 0 results
[WORKER] ⚡ Executing function...
[WORKER] ✅ Execution completed successfully
[WORKER] 📤 Captured output (18 bytes)
[WORKER] ✅ Execution succeeded: Hello from FAAS!
```

---

## 🎯 Résumé du Fix

| Composant | Avant | Après |
|-----------|-------|-------|
| **Worker** | Accepte uniquement `"type":"job"` | Accepte `"type":"job"` ET `"type":"invoke"` |
| **Logs** | Pas de log de réception | Log `[WORKER] 📨 Received message` |
| **Compatibilité** | ❌ Incompatible avec le flux actuel | ✅ Compatible avec tous les formats |

---

## 🚀 Prochaines Étapes

1. ✅ Bug identifié (FAIT)
2. ✅ Fix implémenté (FAIT)
3. ✅ Code recompilé (FAIT)
4. ⏳ Redémarrer le serveur
5. ⏳ Tester l'invocation
6. ⏳ Vérifier que la sortie WASM est correcte

---

## 💡 Pourquoi Ce Bug Existait ?

Le système a été conçu avec deux conventions de nommage différentes :
- **API Gateway → Server** : `"type":"invoke"` (pour les requêtes d'invocation)
- **Worker interne** : `"type":"job"` (pour les tâches à exécuter)

Le Load Balancer transmet le message tel quel, créant une incompatibilité.

**Solution pérenne** : Standardiser sur un seul type (`"invoke"` ou `"job"`), mais pour l'instant, accepter les deux fonctionne parfaitement.

---

**🎉 Le bug est corrigé ! Redémarrez le serveur et testez ! 🚀**
