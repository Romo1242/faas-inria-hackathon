<<<<<<< HEAD
# faas-inria-hackathon
=======
# Mini FaaS in C (Linux)

Plateforme serverless minimaliste implémentée en C pur avec programmation système UNIX/Linux.

## Architecture

```
Client (HTTP) → API Gateway → Server → Load Balancer → Workers
                     ↓            ↓
                 Storage      Pre-fork
```

**Hiérarchie des processus:**
```
Server (processus principal)
    │
    ├─ Worker 0 (fork + execl)
    │   ├─ pipe_to_worker   → stdin
    │   └─ pipe_from_worker ← stdout
    │
    ├─ Worker 1 (fork + execl)
    ├─ Worker 2 (fork + execl)
    └─ Worker 3 (fork + execl)

Load Balancer (processus séparé)
    └─ Distribue les jobs (RR/FIFO)
```

**Flux de requêtes:**

### POST /deploy
```
Client → API Gateway → Vérifie nom unique → Sauvegarde code 
                    → Compile en WASM (si C/Rust) → Retourne ID
```

### POST /invoke
```
Client → API Gateway → Server → Load Balancer (choisit worker)
                              → Server (forward au worker via pipe)
                              → Worker (exécute fonction)
                              → Résultat
```

### GET /function/:name
```
Client → API Gateway → Recherche par nom → Retourne code + métadonnées
```

**Création des workers:**
- **Pre-fork**: Server crée 4 workers au démarrage via `fork()` + `execl()`
- Communication bidirectionnelle via **pipes** (stdin/stdout)
- Enregistrement automatique auprès du **Load Balancer**
- Workers isolés dans des processus séparés

## Composants

- **api_gateway**: Serveur HTTP (port 8080) avec endpoints `/deploy`, `/invoke`, `/function/:name`
- **server**: Crée workers via fork(), gère communication avec pipes, enregistre auprès du LB
- **load_balancer**: Distribue les jobs selon stratégie (RR/FIFO)
- **worker**: Processus isolé exécutant fonctions via Wasmer (WASM) ou runtime natif
- **load_injector**: Outil de test de charge multi-threadé
- **storage**: Gestion persistance fonctions (code + métadonnées)
- **ipc**: Helpers communication (UNIX sockets, pipes, JSON-lines)

## Prérequis

### Obligatoire
- **Linux** ou **WSL2** (Windows Subsystem for Linux)
- `gcc`, `make`

### Pour Compilation WASM (C, Rust, Go)

**C → WASM** (clang + wasi-sdk):
```bash
# Ubuntu/Debian
sudo apt-get install clang lld

# Télécharger wasi-sdk
wget https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-20/wasi-sdk-20.0-linux.tar.gz
tar xvf wasi-sdk-20.0-linux.tar.gz
export PATH=$PATH:$PWD/wasi-sdk-20.0/bin

# Tester
clang --target=wasm32-wasi --version
```

**Rust → WASM**:
```bash
# Installer Rust
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

# Ajouter target wasm32-wasi
rustup target add wasm32-wasi

# Tester
rustc --target wasm32-wasi --version
```

**Go → WASM** (TinyGo):
```bash
# Ubuntu/Debian
wget https://github.com/tinygo-org/tinygo/releases/download/v0.30.0/tinygo_0.30.0_amd64.deb
sudo dpkg -i tinygo_0.30.0_amd64.deb

# Tester
tinygo version
```

### Pour Runtimes Natifs (JS, Python, PHP)

**JavaScript**:
```bash
# Ubuntu/Debian
sudo apt-get install nodejs npm

# Tester
node --version
```

**Python**:
```bash
# Ubuntu/Debian (généralement déjà installé)
sudo apt-get install python3

# Tester
python3 --version
```

**PHP**:
```bash
# Ubuntu/Debian
sudo apt-get install php-cli

# Tester
php --version
```

**HTML**: Aucune installation requise (fichiers statiques)

## Installation

```bash
# Cloner le projet
cd "Hackathon IFRI"

# Compiler
make clean
make
```

Binaires créés dans `build/bin/`:
- `api_gateway`, `server`, `load_balancer`, `worker`, `load_injector`

## Démarrage Rapide

### Méthode 1: Script automatique
```bash
chmod +x scripts/start_all.sh
./scripts/start_all.sh RR
```

### Méthode 2: Manuelle (3 terminaux)

**Terminal 1: Load Balancer**
```bash
./build/bin/load_balancer RR
```
Stratégies: `RR` (Round Robin), `FIFO`

**Terminal 2: Server**
```bash
./build/bin/server
```
Crée automatiquement 4 workers au démarrage

**Terminal 3: API Gateway**
```bash
./build/bin/api_gateway
```
Écoute sur `http://127.0.0.1:8080`

## Utilisation

### 1. Déployer une fonction

```bash
# Fonction C (sera compilée en WASM)
curl -X POST 'http://127.0.0.1:8080/deploy?name=hello&lang=c' \
  --data-binary @examples/hello.c

# Fonction Rust (sera compilée en WASM)
curl -X POST 'http://127.0.0.1:8080/deploy?name=hello_rust&lang=rust' \
  --data-binary @examples/hello.rs

# Fonction Go (sera compilée en WASM avec TinyGo)
curl -X POST 'http://127.0.0.1:8080/deploy?name=hello_go&lang=go' \
  --data-binary @examples/hello.go

# Fonction JavaScript (runtime Node.js)
curl -X POST 'http://127.0.0.1:8080/deploy?name=add&lang=js' \
  --data-binary @examples/add.js

# Fonction Python (runtime Python3)
curl -X POST 'http://127.0.0.1:8080/deploy?name=greet&lang=python' \
  --data-binary @examples/greet.py

# Fonction PHP (runtime PHP)
curl -X POST 'http://127.0.0.1:8080/deploy?name=hello_php&lang=php' \
  --data-binary @examples/hello.php

# Fichier HTML (statique)
curl -X POST 'http://127.0.0.1:8080/deploy?name=index&lang=html' \
  --data-binary @examples/index.html
```

**Réponse:**
```json
{
  "ok": true,
  "id": "hello_1728435600",
  "name": "hello",
  "lang": "c",
  "wasm": true
}
```

### 2. Invoquer une fonction

```bash
# Utiliser l'ID retourné lors du déploiement
curl -X POST 'http://127.0.0.1:8080/invoke?fn=hello_1728435600' \
  -d '{"input":"test"}'
```

**Réponse:**
```json
{
  "ok": true,
  "output": "Hello from FAAS!\n"
}
```

### 3. Récupérer le code source

```bash
curl -X GET 'http://127.0.0.1:8080/function/hello'
```

**Réponse:**
```json
{
  "ok": true,
  "id": "hello_1728435600",
  "name": "hello",
  "lang": "c",
  "size": 123,
  "code": "// Simple C function..."
}
```

### 4. Test de charge

```bash
# Déployer une fonction
curl -X POST 'http://127.0.0.1:8080/deploy?name=hello&lang=c' \
  --data-binary @examples/hello.c

# Tester avec 10 threads, 100 requêtes/thread
./build/bin/load_injector hello_1728435600 10 100
```

### 5. Script de test complet

```bash
chmod +x scripts/test_system.sh
./scripts/test_system.sh
```

## Sockets/Paths

- API Gateway: `127.0.0.1:8080` (TCP)
- Load Balancer: `/tmp/faas_lb.sock` (UNIX)
- Server: `/tmp/faas_server.sock` (UNIX)

## Protocol (JSON Lines)

- **API → Server**: `{"type":"invoke","fn":"name","payload":"..."}\n`
- **Server → LB**: `{"type":"invoke","fn":"name","payload":"..."}\n`
- **LB → Worker** (via pipe): `{"type":"job","fn":"name","payload":"..."}\n`
- **Worker → LB** (via pipe): `{"ok":true,"output":"..."}\n`
- **LB → Server**: `{"ok":true,"output":"..."}\n`
- **Server → API**: `{"ok":true,"output":"..."}\n`

## Fonctionnalités Implémentées

### ✅ Complétées
- **Architecture complète**: API Gateway → Server → Load Balancer → Workers
- **Pre-fork workers**: 4 workers créés au démarrage via `fork()` + `execl()`
- **Communication IPC**: UNIX sockets + pipes bidirectionnels
- **Load balancing**: Round Robin (RR) et FIFO
- **Endpoints HTTP**:
  - `POST /deploy` - Déploiement avec vérification nom unique
  - `POST /invoke` - Invocation de fonction
  - `GET /function/:name` - Récupération code source
- **Compilation WASM**: Automatique pour C/Rust lors du deploy
- **Multi-langages**: C (WASM), JavaScript (Node), Python (Python3)
- **Persistence**: Stockage fonctions + métadonnées JSON
- **Load injector**: Test de charge multi-threadé
- **Scripts**: Démarrage automatique et tests

### ⏳ À Implémenter
- **Wasmer runtime**: Intégration complète (stub prêt, nécessite libwasmer)
- **WEIGHTED load balancing**: Distribution basée sur la charge
- **Auto-scaling**: Création/destruction dynamique de workers
- **Monitoring**: Métriques (latence, throughput, erreurs)
- **Sécurité**: Sandboxing, limites ressources (cgroups)

## Langages Supportés

| Langage | Extension | Compilation | Runtime | Installation |
|---------|-----------|-------------|---------|--------------|
| **C** | .c | ✅ WASM (clang) | Wasmer | clang + wasi-sdk |
| **Rust** | .rs | ✅ WASM (rustc) | Wasmer | rustc + wasm32-wasi |
| **Go** | .go | ✅ WASM (tinygo) | Wasmer | tinygo |
| **JavaScript** | .js | ❌ Natif | Node.js | nodejs |
| **Python** | .py | ❌ Natif | Python3 | python3 |
| **PHP** | .php | ❌ Natif | PHP CLI | php-cli |
| **HTML** | .html | ❌ Statique | - | Aucune |

### Avantages par Type

**WASM (C, Rust, Go)**:
- ✅ Portable (même binaire partout)
- ✅ Sécurisé (sandboxing WASI)
- ✅ Rapide (compilation AOT)
- ✅ Isolation complète

**Runtime Natif (JS, Python, PHP)**:
- ✅ Simple à déployer
- ✅ Pas de compilation
- ✅ Accès bibliothèques natives
- ⚠️ Moins sécurisé (accès système)

**Statique (HTML)**:
- ✅ Aucune exécution
- ✅ Retour direct du contenu
- ✅ Idéal pour pages web

## Wasmer Integration (TODO)

The worker has a stub for Wasmer runtime integration. To enable:

1. Install Wasmer C API:
```bash
curl https://get.wasmer.io -sSfL | sh
```

2. Compile functions to WASM:
```bash
# C to WASM
clang --target=wasm32-wasi -o function.wasm function.c

# Rust to WASM
rustc --target wasm32-wasi function.rs -o function.wasm
```

3. Update `src/worker.c` to use Wasmer C API in `execute_function()`

## Documentation

- **[QUICKSTART.md](QUICKSTART.md)**: Guide de démarrage rapide avec exemples
- **[ARCHITECTURE.md](ARCHITECTURE.md)**: Architecture détaillée et concepts système
- **[DEPLOY.md](DEPLOY.md)**: Guide de déploiement

## Dépannage

### Nettoyer les sockets après crash
```bash
rm /tmp/faas_*.sock
```

### Vérifier les processus
```bash
ps aux | grep -E "api_gateway|server|load_balancer|worker"
```

### Voir les logs (si démarré avec script)
```bash
tail -f logs/*.log
```

### Erreurs courantes

**"server unavailable"**: Le server n'est pas démarré
```bash
./build/bin/server
```

**"no worker available"**: Pas de workers actifs
- Vérifiez que le server a créé les workers
- Regardez les logs du server

**"function name already exists"**: Nom déjà utilisé
- Choisissez un autre nom ou supprimez l'ancienne fonction
```bash
rm -rf functions/<old_function_id>/
```

## Concepts Clés de Programmation Système

Ce projet démontre plusieurs concepts avancés:

1. **fork() + execl()**: Création de processus isolés
2. **Pipes**: Communication inter-processus bidirectionnelle
3. **UNIX Sockets**: IPC rapide et sécurisé
4. **Signaux**: Gestion SIGCHLD, SIGINT, SIGTERM
5. **Pre-fork**: Pattern serveur haute performance
6. **Load Balancing**: Distribution de charge (RR, FIFO)
7. **JSON-Lines**: Protocole de communication simple
8. **WASM**: Compilation et exécution portable

## Contribution

Pour contribuer:
1. Fork le projet
2. Créer une branche feature
3. Tester avec `make && ./scripts/test_system.sh`
4. Soumettre une Pull Request

## Licence

MIT License - Voir LICENSE pour détails
>>>>>>> 14440c8 (Initial commit - upload du projet FaaS Hackathon)
