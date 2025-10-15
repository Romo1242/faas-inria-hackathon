# 🚀 Serveur Unifié - Mini FaaS

## 📋 Vue d'Ensemble

Le système a été refactorisé pour utiliser **UN SEUL BINAIRE** : `./build/bin/server`

Ce binaire unique démarre automatiquement :
1. ✅ Load Balancer (UNIX socket)
2. ✅ API Gateway (HTTP :8080)
3. ✅ Server + 4 Workers (fork avec Wasmer initialisé)

## 🏗️ Architecture

```
./build/bin/server (binaire unique)
    │
    ├─→ Thread 1: Load Balancer
    │   └─ UNIX socket: /tmp/faas_lb.sock
    │   └─ Stratégies: RR, FIFO, WEIGHTED
    │
    ├─→ Thread 2: API Gateway
    │   └─ HTTP server: 127.0.0.1:8080
    │   └─ Endpoints: /deploy, /invoke, /function/:name
    │   └─ Compile C/Rust/Go → WASM
    │
    └─→ Process: Server
        └─ fork() × 4 → Workers
            ├─ Worker 0 (PID xxxx) ← Wasmer init ✅
            ├─ Worker 1 (PID xxxx) ← Wasmer init ✅
            ├─ Worker 2 (PID xxxx) ← Wasmer init ✅
            └─ Worker 3 (PID xxxx) ← Wasmer init ✅
```

## 🔧 Compilation

```bash
cd /mnt/c/Users/Dell/Documents/Hackathon\ IFRI

# Nettoyer et compiler
make clean && make

# Binaires créés:
# - build/bin/server        (binaire unifié)
# - build/bin/worker        (utilisé par server via fork)
# - build/bin/load_injector (test de charge)
```

## 🚀 Démarrage

### Option 1 : Avec Stratégie par Défaut (RR)

```bash
./build/bin/server
```

### Option 2 : Avec Stratégie Spécifique

```bash
# Round Robin
./build/bin/server RR

# FIFO
./build/bin/server FIFO

# Weighted (à implémenter)
./build/bin/server WEIGHTED
```

### Sortie Attendue

```
╔═══════════════════════════════════════════════════════╗
║         🚀 Mini FaaS - Unified Server 🚀            ║
╚═══════════════════════════════════════════════════════╝

[MAIN] Stratégie de load balancing: RR
[MAIN] PID principal: 12345

[LB] 🔄 Démarrage du Load Balancer (stratégie: RR)...
[API] 🌐 Démarrage de l'API Gateway (port 8080)...
[SERVER] 👷 Démarrage du Server et création des workers...

╔═══════════════════════════════════════════════════════╗
║              ✅ Système Démarré !                    ║
╚═══════════════════════════════════════════════════════╝

📊 Composants actifs:
  • Load Balancer:  /tmp/faas_lb.sock
  • API Gateway:    http://127.0.0.1:8080
  • Server:         PID 12346 (+ 4 workers)

🧪 Pour tester:
  curl -X POST 'http://127.0.0.1:8080/deploy?name=hello&lang=c' \
    -H "Content-Type: text/plain" --data-binary @examples/hello.c

  curl -X POST 'http://127.0.0.1:8080/invoke?fn=hello_<timestamp>'

⏹️  Appuyez sur Ctrl+C pour arrêter...
```

## 🧪 Tests

### 1. Déployer une Fonction C

```bash
curl -X POST 'http://127.0.0.1:8080/deploy?name=hello&lang=c' \
    -H "Content-Type: text/plain" \
    --data-binary @examples/hello.c
```

**Réponse:**
```json
{
  "ok": true,
  "id": "hello_1760055556",
  "name": "hello",
  "lang": "c",
  "wasm": true
}
```

### 2. Invoquer la Fonction (Exécution WASM)

```bash
curl -X POST 'http://127.0.0.1:8080/invoke?fn=hello_1760055556'
```

**Réponse:**
```json
{
  "ok": true,
  "output": "Hello from FAAS!\n"
}
```

### 3. Récupérer le Code Source

```bash
curl -X GET 'http://127.0.0.1:8080/function/hello'
```

**Réponse:**
```json
{
  "ok": true,
  "id": "hello_1760055556",
  "name": "hello",
  "lang": "c",
  "size": 123,
  "code": "#include <stdio.h>\n\nint main() {\n    printf(\"Hello from FAAS!\\n\");\n    return 0;\n}\n"
}
```

## 📊 Langages Supportés

### Compilation WASM (Exécutés avec Wasmer)

| Langage | Extension | Compilateur | Exécution |
|---------|-----------|-------------|-----------|
| **C** | .c | /opt/wasi-sdk/bin/clang | Wasmer + WASI |
| **Rust** | .rs | rustc --target wasm32-wasi | Wasmer + WASI |
| **Go** | .go | tinygo -target=wasi | Wasmer + WASI |

### Runtime Natif (Pas de WASM)

| Langage | Extension | Runtime | Exécution |
|---------|-----------|---------|-----------|
| **JavaScript** | .js | node | popen("node code.js") |
| **Python** | .py | python3 | popen("python3 code.py") |
| **PHP** | .php | php-cli | popen("php code.php") |
| **HTML** | .html | - | Retour direct du contenu |

## 🔄 Flux d'Exécution WASM

```
1. Client → POST /deploy?name=hello&lang=c
   ↓
2. API Gateway:
   - Sauvegarde code.c dans functions/hello_XXX/
   - Compile: /opt/wasi-sdk/bin/clang → code.wasm
   - Retourne: {"ok":true,"id":"hello_XXX","wasm":true}
   ↓
3. Client → POST /invoke?fn=hello_XXX
   ↓
4. API Gateway → Server (UNIX socket)
   ↓
5. Server → Load Balancer (UNIX socket)
   ↓
6. Load Balancer:
   - Choisit worker (RR/FIFO)
   - Retourne: {"forward_to_worker":2}
   ↓
7. Server → Worker 2 (pipe stdin)
   - Envoie: {"type":"job","fn":"hello_XXX"}
   ↓
8. Worker 2:
   - fork() processus fils
   - Charge code.wasm
   - Initialise Wasmer (engine + store + WASI)
   - Exécute fonction _start
   - Capture stdout via pipe
   - Retourne: {"ok":true,"output":"Hello from FAAS!\n"}
   ↓
9. Worker 2 → Server (pipe stdout)
   ↓
10. Server → API Gateway
    ↓
11. API Gateway → Client
    - Retourne: {"ok":true,"output":"Hello from FAAS!\n"}
```

## 🛑 Arrêt du Système

### Arrêt Gracieux

```bash
# Appuyez sur Ctrl+C dans le terminal où server tourne
```

Le système arrêtera proprement :
1. Load Balancer
2. API Gateway
3. Server
4. Tous les workers

### Arrêt Forcé

```bash
# Trouver le PID
ps aux | grep "build/bin/server"

# Tuer le processus
kill -9 <PID>

# Nettoyer les sockets
rm -f /tmp/faas_*.sock
```

## 📝 Fichiers Modifiés

### Nouveaux Fichiers

1. **`src/main_server.c`** : Point d'entrée unique
   - Démarre LB (thread)
   - Démarre API (thread)
   - Démarre Server (fork)
   - Gère signaux et shutdown

### Fichiers Modifiés

1. **`src/load_balancer.c`** :
   - `int main()` → `int load_balancer_main()`

2. **`src/api_gateway.c`** :
   - `int main()` → `int api_gateway_main()`

3. **`src/server.c`** :
   - `int main()` → `int server_main()`

4. **`src/worker.c`** :
   - Intégration complète de Wasmer avec WASI
   - Capture de sortie avec fork + pipe

5. **`Makefile`** :
   - Binaire unifié : `server` (compile main_server.c + api_gateway.c + load_balancer.c + server.c)
   - Worker avec Wasmer : `-lwasmer -ldl`

## ✅ Avantages du Binaire Unifié

1. **Simplicité** : Une seule commande pour tout démarrer
2. **Logs centralisés** : Tous les logs dans un seul terminal
3. **Gestion simplifiée** : Un seul processus à monitorer
4. **Déploiement facile** : Un seul binaire à distribuer
5. **Debugging** : Plus facile de suivre le flux complet

## 🐛 Dépannage

### Erreur : `cannot open shared object file: libwasmer.so`

```bash
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
sudo ldconfig
```

### Erreur : `Address already in use (port 8080)`

```bash
# Trouver le processus qui utilise le port
lsof -i :8080

# Tuer le processus
kill -9 <PID>
```

### Erreur : `Permission denied: /tmp/faas_lb.sock`

```bash
# Nettoyer les anciens sockets
rm -f /tmp/faas_*.sock
```

### Les workers ne démarrent pas

```bash
# Vérifier que le binaire worker existe
ls -la build/bin/worker

# Vérifier les permissions
chmod +x build/bin/worker
```

## 🚀 Prochaines Étapes

1. ✅ Installer Wasmer
2. ✅ Installer wasi-sdk dans /opt/
3. ✅ Compiler le projet
4. ✅ Tester le déploiement C → WASM
5. ✅ Tester l'invocation avec exécution WASM
6. ⏳ Ajouter métriques (latence, throughput)
7. ⏳ Implémenter stratégie WEIGHTED
8. ⏳ Ajouter dashboard de monitoring

## 📚 Documentation Complète

- **[README.md](README.md)** : Vue d'ensemble du projet
- **[QUICKSTART.md](QUICKSTART.md)** : Guide de démarrage rapide
- **[LANGUAGES.md](LANGUAGES.md)** : Guide des langages supportés
- **[INSTALL_WASMER.md](INSTALL_WASMER.md)** : Installation de Wasmer
- **[INSTALL_COMPILERS.md](INSTALL_COMPILERS.md)** : Installation des compilateurs
- **[ARCHITECTURE.md](ARCHITECTURE.md)** : Architecture technique détaillée
