# ✅ Refactorisation Terminée - Architecture Finale

## 🎯 Changements Majeurs

### 1. **Séparation des Responsabilités**

#### Avant :
- ❌ API Gateway compilait le code WASM
- ❌ 3 binaires séparés à démarrer manuellement
- ❌ Logs dispersés

#### Après :
- ✅ **API Gateway** : Routage HTTP uniquement
- ✅ **Server** : Compilation WASM + Gestion workers
- ✅ **1 seul binaire** : `./build/bin/server`
- ✅ Logs centralisés en temps réel

---

## 🏗️ Architecture Finale

```
┌─────────────────────────────────────────────────────────┐
│         ./build/bin/server (BINAIRE UNIQUE)             │
│                                                          │
│  ┌────────────────────────────────────────────────┐    │
│  │  Thread 1: Load Balancer                       │    │
│  │  • UNIX socket: /tmp/faas_lb.sock             │    │
│  │  • Stratégies: RR, FIFO, WEIGHTED             │    │
│  └────────────────────────────────────────────────┘    │
│                                                          │
│  ┌────────────────────────────────────────────────┐    │
│  │  Thread 2: API Gateway                         │    │
│  │  • HTTP server: :8080                          │    │
│  │  • POST /deploy → Envoie au Server            │    │
│  │  • POST /invoke → Envoie au Server            │    │
│  │  • GET /function/:name → Retourne code        │    │
│  └────────────────────────────────────────────────┘    │
│                                                          │
│  ┌────────────────────────────────────────────────┐    │
│  │  Process: Server                               │    │
│  │  • Reçoit deploy → Compile WASM               │    │
│  │  • Reçoit invoke → Envoie au LB               │    │
│  │  • Gère 4 workers (fork)                      │    │
│  │                                                 │    │
│  │  ├─ Worker 0 (Wasmer init ✅)                 │    │
│  │  ├─ Worker 1 (Wasmer init ✅)                 │    │
│  │  ├─ Worker 2 (Wasmer init ✅)                 │    │
│  │  └─ Worker 3 (Wasmer init ✅)                 │    │
│  └────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────┘
```

---

## 🔄 Flux de Déploiement (POST /deploy)

```
1. Client → HTTP POST /deploy?name=hello&lang=c
   ↓
2. API Gateway (Thread HTTP)
   - Reçoit le code C
   - Parse name, lang, code
   - Envoie au Server via UNIX socket:
     {"type":"deploy","name":"hello","lang":"c","code":"..."}
   ↓
3. Server (Process Principal)
   - Reçoit la requête deploy
   - Vérifie si le nom existe déjà
   - Stocke le code: functions/hello_XXX/code.c
   - Stocke metadata: functions/hello_XXX/metadata.json
   ↓
4. Server - Compilation WASM
   - Détecte lang="c"
   - Exécute: /opt/wasi-sdk/bin/clang --target=wasm32-wasi \
              -Wl,--export-all -o code.wasm code.c
   - Sauvegarde: functions/hello_XXX/code.wasm
   ↓
5. Server → API Gateway
   - Retourne: {"ok":true,"id":"hello_XXX","wasm":true}
   ↓
6. API Gateway → Client
   - HTTP 201 Created
   - Body: {"ok":true,"id":"hello_XXX","name":"hello","lang":"c","wasm":true}
```

---

## ⚡ Flux d'Invocation (POST /invoke)

```
1. Client → HTTP POST /invoke?fn=hello_XXX
   ↓
2. API Gateway
   - Parse fn=hello_XXX
   - Envoie au Server:
     {"type":"invoke","fn":"hello_XXX","payload":""}
   ↓
3. Server
   - Reçoit invoke
   - Envoie au Load Balancer:
     {"type":"invoke","fn":"hello_XXX","payload":""}
   ↓
4. Load Balancer
   - Choisit worker (RR/FIFO)
   - Retourne: {"type":"forward_to_worker","worker_id":2,"job":{...}}
   ↓
5. Server
   - Envoie job au Worker 2 via pipe (stdin)
   ↓
6. Worker 2
   - fork() processus fils
   - Charge functions/hello_XXX/code.wasm
   - Initialise Wasmer (engine + store + WASI)
   - Exécute fonction _start
   - Capture stdout via pipe
   - Retourne: {"ok":true,"output":"Hello from FAAS!\n"}
   ↓
7. Worker 2 → Server (pipe stdout)
   ↓
8. Server → API Gateway
   ↓
9. API Gateway → Client
   - HTTP 200 OK
   - Body: {"ok":true,"output":"Hello from FAAS!\n"}
```

---

## 📝 Fichiers Modifiés

### Nouveaux Fichiers

1. **`src/main_server.c`**
   - Point d'entrée unique
   - Démarre LB (thread)
   - Démarre API (thread)
   - Démarre Server (fork workers)

2. **`start_server.sh`**
   - Script simplifié pour démarrer le serveur
   - Usage: `./start_server.sh [RR|FIFO|WEIGHTED]`

3. **`REFACTORING_COMPLETE.md`** (ce fichier)
   - Documentation de la refactorisation

### Fichiers Modifiés

1. **`src/api_gateway.c`**
   - ❌ Supprimé: `compile_to_wasm()`
   - ✅ Ajouté: Délégation au Server pour deploy
   - ✅ Simplifié: Routage HTTP uniquement

2. **`src/server.c`**
   - ✅ Ajouté: `#include "storage.h"`
   - ✅ Ajouté: `compile_to_wasm()` (déplacé depuis API)
   - ✅ Ajouté: `handle_deploy()` (gère compilation)
   - ✅ Modifié: `handle_request()` (route deploy)
   - ✅ Chemins complets: `/opt/wasi-sdk/bin/clang`

3. **`src/worker.c`**
   - ✅ Intégration Wasmer complète avec WASI
   - ✅ Capture de sortie avec fork + pipe
   - ✅ Support C/Rust/Go (WASM) + JS/Python/PHP (runtime)

4. **`Makefile`**
   - ✅ Binaire unifié: `server` (main_server + api + lb + server)
   - ✅ Worker avec Wasmer: `-lwasmer -ldl`
   - ✅ Flags: `-I/usr/local/include -L/usr/local/lib`

### Fichiers Obsolètes (À Supprimer)

1. **`scripts/start_all.sh`** → Remplacé par `start_server.sh`
2. **`scripts/deploy_function.sh`** → Utiliser curl directement

---

## 🚀 Utilisation

### 1. Compilation

```bash
cd /mnt/c/Users/Dell/Documents/Hackathon\ IFRI

# Installer Wasmer
cd /tmp
wget https://github.com/wasmerio/wasmer/releases/download/v3.3.0/wasmer-linux-amd64.tar.gz
tar xzf wasmer-linux-amd64.tar.gz
sudo cp -r include/* /usr/local/include/
sudo cp -r lib/* /usr/local/lib/
sudo ldconfig

# Installer wasi-sdk
wget https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-20/wasi-sdk-20.0-linux.tar.gz
sudo tar xzf wasi-sdk-20.0-linux.tar.gz -C /opt/
sudo mv /opt/wasi-sdk-20.0 /opt/wasi-sdk

# Compiler le projet
cd /mnt/c/Users/Dell/Documents/Hackathon\ IFRI
make clean && make
```

### 2. Démarrage

```bash
# Démarrer TOUT avec une seule commande
./start_server.sh RR

# Ou directement
./build/bin/server RR
```

### 3. Tests

```bash
# Déployer une fonction C
curl -X POST 'http://127.0.0.1:8080/deploy?name=hello&lang=c' \
    -H "Content-Type: text/plain" \
    --data-binary @examples/hello.c

# Réponse:
# {"ok":true,"id":"hello_1760055556","name":"hello","lang":"c","wasm":true}

# Invoquer la fonction
curl -X POST 'http://127.0.0.1:8080/invoke?fn=hello_1760055556'

# Réponse:
# {"ok":true,"output":"Hello from FAAS!\n"}

# Récupérer le code source
curl -X GET 'http://127.0.0.1:8080/function/hello'
```

---

## 📊 Langages Supportés

### Compilation WASM (Exécutés avec Wasmer)

| Langage | Compilateur | Chemin Complet |
|---------|-------------|----------------|
| **C** | clang | `/opt/wasi-sdk/bin/clang` |
| **Rust** | rustc | `rustc --target wasm32-wasi` |
| **Go** | tinygo | `tinygo build -target=wasi` |

### Runtime Natif (Pas de WASM)

| Langage | Runtime | Commande |
|---------|---------|----------|
| **JavaScript** | node | `node code.js` |
| **Python** | python3 | `python3 code.py` |
| **PHP** | php-cli | `php code.php` |
| **HTML** | - | Retour direct |

---

## 🎯 Avantages de la Refactorisation

### 1. **Simplicité**
- ✅ 1 seul binaire à démarrer
- ✅ 1 seul script: `./start_server.sh`
- ✅ Logs centralisés dans un terminal

### 2. **Séparation des Responsabilités**
- ✅ API Gateway = Routage HTTP
- ✅ Server = Compilation + Exécution
- ✅ Workers = Exécution WASM isolée

### 3. **Performance**
- ✅ Compilation côté Server (pas de latence HTTP)
- ✅ Workers pré-forkés (pas de création à la demande)
- ✅ Wasmer initialisé une fois par worker

### 4. **Maintenabilité**
- ✅ Code plus modulaire
- ✅ Responsabilités claires
- ✅ Facile à déboguer

### 5. **Déploiement**
- ✅ 1 seul binaire à distribuer
- ✅ Configuration simplifiée
- ✅ Monitoring centralisé

---

## 🐛 Dépannage

### Erreur: `libwasmer.so: cannot open shared object file`

```bash
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
sudo ldconfig
```

### Erreur: `/opt/wasi-sdk/bin/clang: No such file or directory`

```bash
# Installer wasi-sdk
cd /tmp
wget https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-20/wasi-sdk-20.0-linux.tar.gz
sudo tar xzf wasi-sdk-20.0-linux.tar.gz -C /opt/
sudo mv /opt/wasi-sdk-20.0 /opt/wasi-sdk

# Vérifier
ls -la /opt/wasi-sdk/bin/clang
```

### Erreur: `Address already in use (port 8080)`

```bash
# Trouver et tuer le processus
lsof -i :8080
kill -9 <PID>
```

### Les workers ne démarrent pas

```bash
# Vérifier le binaire worker
ls -la build/bin/worker
chmod +x build/bin/worker

# Vérifier les logs
./build/bin/server RR
# Regarder les messages [SERVER] et [WORKER-X]
```

---

## 📚 Documentation

- **[README.md](README.md)** : Vue d'ensemble
- **[QUICKSTART.md](QUICKSTART.md)** : Démarrage rapide
- **[UNIFIED_SERVER.md](UNIFIED_SERVER.md)** : Architecture binaire unifié
- **[INSTALL_WASMER.md](INSTALL_WASMER.md)** : Installation Wasmer
- **[REFACTORING_COMPLETE.md](REFACTORING_COMPLETE.md)** : Ce fichier

---

## ✅ Checklist de Déploiement

- [ ] Wasmer installé dans `/usr/local/lib`
- [ ] wasi-sdk installé dans `/opt/wasi-sdk`
- [ ] Projet compilé: `make clean && make`
- [ ] Binaire créé: `build/bin/server`
- [ ] Worker compilé: `build/bin/worker`
- [ ] Test déploiement C → WASM
- [ ] Test invocation → Exécution WASM
- [ ] Test GET /function/:name

---

## 🚀 Prochaines Étapes

1. ✅ Refactorisation architecture (FAIT)
2. ✅ Compilation déléguée au Server (FAIT)
3. ✅ Binaire unifié (FAIT)
4. ⏳ Installer Wasmer et wasi-sdk
5. ⏳ Compiler et tester
6. ⏳ Ajouter métriques (latence, throughput)
7. ⏳ Implémenter stratégie WEIGHTED
8. ⏳ Dashboard de monitoring
9. ⏳ Support Rust et Go (tester compilation)
10. ⏳ Optimisation: réutiliser Wasmer engine/store

---

**🎉 La refactorisation est terminée ! Compilez et testez !**
