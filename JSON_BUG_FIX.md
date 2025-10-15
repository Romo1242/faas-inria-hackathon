# 🐛 BUG CRITIQUE CORRIGÉ : JSON Malformé

## ❌ Problème Identifié

Dans `src/load_balancer.c` ligne 144, le JSON était **cassé** :

### Avant (BUGUÉ)

```c
snprintf(job, sizeof(job), "{\"type\":\"job\",%.*s}\n", 
         (int)(strlen(line) - 20), fnp);
```

**Résultat** :
```json
{"type":"job","fn":"add_1760083773","payload":"}
                                              ↑
                                      CASSÉ ICI !
```

Le JSON est **incomplet** :
- ❌ Second guillemet de `payload` manquant
- ❌ Accolade fermante manquante
- ❌ Calcul arbitraire `strlen(line) - 20` incorrect

---

## ✅ Solution Appliquée

### Après (CORRIGÉ)

```c
// Parse fn and payload values
char func_id[256] = {0};
char payload[LINE_MAX] = {0};
sscanf(fnp, "\"fn\":\"%255[^\"]\"", func_id);
sscanf(plp, "\"payload\":\"%8191[^\"]\"", payload);

// Build job line for worker
char job[LINE_MAX];
snprintf(job, sizeof(job), "{\"type\":\"job\",\"fn\":\"%s\",\"payload\":\"%s\"}\n", 
         func_id, payload);
```

**Résultat** :
```json
{"type":"job","fn":"add_1760083773","payload":""}
```

✅ JSON **valide et complet** !

---

## 🧪 Test du Fix

### 1. Redémarrer le Serveur

```bash
# Dans le terminal du serveur (tty2), arrêtez avec Ctrl+C

# Puis redémarrez
cd /mnt/c/Users/Dell/Documents/Hackathon\ IFRI
./start_server.sh RR
```

### 2. Tester avec invoke.sh

```bash
./invoke.sh
```

**OU** manuellement :

```bash
# Déployer
curl -X POST 'http://127.0.0.1:8080/deploy?name=add&lang=c' \
    --data-binary - << 'EOF'
int add(int a, int b) {
    return a + b;
}

int main() {
    return add(21, 21);
}
EOF

# Noter l'ID retourné (ex: add_1760083773)

# Invoquer
curl -X POST 'http://127.0.0.1:8080/invoke?fn=add_1760083773'
```

---

## 📊 Logs Attendus (COMPLETS)

Avec le fix, vous verrez :

```
[SERVER] 🔄 Forwarding to Load Balancer: {"type":"invoke","fn":"add_XXX","payload":""}
[SERVER] ✅ Connected to Load Balancer
[SERVER] 📤 Sending to LB: {"type":"invoke","fn":"add_XXX","payload":""}
[SERVER] ⏳ Waiting for response from LB...

[LB] 📨 Received invoke request: {"type":"invoke","fn":"add_XXX","payload":""}
[LB] ✅ Selected worker 0 (PID 10915)
[LB] 📦 Built job: {"type":"job","fn":"add_XXX","payload":""}    ← ✅ JSON VALIDE !
[LB] 🔗 Connecting to Server...
[LB] ✅ Connected to Server
[LB] 📤 Sending to Server: {"type":"forward_to_worker","worker_id":0,"job":{"type":"job","fn":"add_XXX","payload":""}
}
[LB] ⏳ Waiting for response from Server...

[SERVER] 📨 Received forward_to_worker from LB: ...
[SERVER] 🎯 Target worker: 0
[SERVER] 📤 Sending job to worker 0 via pipe: {"type":"job","fn":"add_XXX","payload":""}
[SERVER] ⏳ Waiting for response from worker 0...

[WORKER] 📨 Received message: {"type":"job","fn":"add_XXX","payload":""}
[WORKER] 📨 Received job: fn=add_XXX, payload=
[WORKER] 🚀 Calling execute_function()...
[WORKER] 🔍 Loading metadata for: add_XXX
[WORKER] ✅ Metadata loaded: lang=c
[WORKER] 🎯 Detected WASM language: c
[WORKER] ✅ WASM file found: functions/add_XXX/code.wasm
[WORKER] 🚀 Executing WASM with Wasmer...
[WORKER] 🚀 Execute WASM directly in worker process (PID 10915)
[WORKER] 📂 Loading WASM file: functions/add_XXX/code.wasm
[WORKER] 📦 Loaded WASM file: 8243 bytes
[WORKER] 🔧 Initializing Wasmer engine...
[WORKER] ✅ WASM module compiled
[WORKER] ✅ WASI environment created
[WORKER] ✅ WASI imports retrieved
[WORKER] ✅ WASM instance created
[WORKER] ✅ Found function: _start
[WORKER] 🔧 Function signature: 0 params, 0 results
[WORKER] ⚡ Executing WASM function...
[WORKER] ✅ Execution completed successfully
[WORKER] 📤 Captured output (0 bytes): 
[WORKER] ✅ Wasmer cleanup complete
[WORKER] ✅ Execution succeeded: 

[SERVER] ✅ Received response from worker 0: {"ok":true,"output":""}
[SERVER] 🏁 forward_to_worker completed

[LB] ✅ Received response from Server: {"ok":true,"output":""}
[LB] 🏁 Request completed

[SERVER] ✅ Received response from LB: {"ok":true,"output":""}
[SERVER] 🏁 Request completed
```

---

## 🎯 Résultat Attendu

```bash
curl -X POST 'http://127.0.0.1:8080/invoke?fn=add_1760083773'
```

**Réponse** :
```json
{"ok":true,"output":""}
```

**Note** : `output` est vide car le programme C ne fait pas de `printf()`. Il retourne juste `42` (21+21) comme code de sortie, mais ce n'est pas capturé dans stdout.

---

## 💡 Pour Voir la Sortie

Modifiez la fonction pour qu'elle imprime :

```c
#include <stdio.h>

int add(int a, int b) {
    return a + b;
}

int main() {
    int result = add(21, 21);
    printf("Result: %d\n", result);
    return 0;
}
```

Redéployez et réinvoquez, vous verrez :

```json
{"ok":true,"output":"Result: 42\n"}
```

---

## 📋 Résumé du Fix

| Aspect | Avant | Après |
|--------|-------|-------|
| **Construction JSON** | Copie partielle avec `%.*s` | Parse puis reconstruit |
| **Calcul longueur** | `strlen(line) - 20` arbitraire | Parse avec `sscanf()` |
| **JSON généré** | ❌ Cassé | ✅ Valide |
| **Worker** | Ne reconnaît pas le JSON | Reconnaît et exécute |

---

## ✅ Conclusion

Le bug était dans **la construction du message JSON** par le Load Balancer. Au lieu de copier aveuglément une partie du JSON d'entrée, nous **parsons les valeurs** et **reconstruisons proprement** le JSON de sortie.

**Le système devrait maintenant fonctionner de bout en bout ! 🎉**
