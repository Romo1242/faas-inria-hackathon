# Guide de Démarrage Rapide - Mini FaaS

## Architecture du Système

```
┌─────────────┐
│   Client    │
└──────┬──────┘
       │ HTTP (port 8080)
       ▼
┌─────────────────────────────────────────────────────────────┐
│                      API Gateway                            │
│  - POST /deploy   → Sauvegarde + Compile en WASM           │
│  - POST /invoke   → Exécute fonction                        │
│  - GET /function/:name → Récupère code source              │
└──────┬──────────────────────────────────────────────────────┘
       │ UNIX Socket (/tmp/faas_server.sock)
       ▼
┌─────────────────────────────────────────────────────────────┐
│                         Server                              │
│  - Démarre 4 workers au démarrage (pre-fork)               │
│  - Enregistre les workers auprès du Load Balancer          │
│  - Transmet les jobs aux workers via pipes                 │
└──────┬──────────────────────────────────────────────────────┘
       │ UNIX Socket (/tmp/faas_lb.sock)
       ▼
┌─────────────────────────────────────────────────────────────┐
│                    Load Balancer                            │
│  - Stratégies: RR (Round Robin) / FIFO                     │
│  - Distribue les requêtes aux workers disponibles          │
└──────┬──────────────────────────────────────────────────────┘
       │ Via Server (pipes)
       ▼
┌─────────────────────────────────────────────────────────────┐
│                       Workers (x4)                          │
│  - Processus isolés créés par fork()                       │
│  - Exécutent les fonctions via Wasmer (WASM) ou runtime   │
│  - Communication bidirectionnelle via pipes                │
└─────────────────────────────────────────────────────────────┘
```

## Flux de Requêtes

### 1. POST /deploy (Déploiement)
```
Client → API Gateway → Vérifie nom unique → Sauvegarde code → Compile en WASM → Retourne ID
```

### 2. POST /invoke (Invocation)
```
Client → API Gateway → Server → Load Balancer (RR/FIFO) → Server → Worker → Exécution → Résultat
```

### 3. GET /function/:name (Récupération)
```
Client → API Gateway → Recherche par nom → Retourne code + métadonnées
```

## Installation et Compilation

### Prérequis
- **Linux** ou **WSL2** (Windows Subsystem for Linux)
- `gcc`, `make`
- `clang` avec support `wasm32-wasi` (pour compiler C en WASM)
- `node` (pour exécuter JavaScript)
- `python3` (pour exécuter Python)

### Installation de clang avec WASM
```bash
# Ubuntu/Debian
sudo apt-get install clang lld

# Installer wasi-sdk pour compilation WASM
wget https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-20/wasi-sdk-20.0-linux.tar.gz
tar xvf wasi-sdk-20.0-linux.tar.gz
export PATH=$PATH:$PWD/wasi-sdk-20.0/bin
```

### Compilation
```bash
cd "Hackathon IFRI"
make clean
make
```

Cela créera les binaires dans `build/bin/` :
- `api_gateway`
- `server`
- `load_balancer`
- `worker`
- `load_injector`

## Démarrage du Système

**Important**: Démarrer dans l'ordre suivant (3 terminaux différents)

### Terminal 1: Load Balancer
```bash
./build/bin/load_balancer RR
```
Stratégies disponibles: `RR` (Round Robin), `FIFO`

### Terminal 2: Server
```bash
./build/bin/server
```
Le serveur va automatiquement:
- Créer 4 workers au démarrage
- Les enregistrer auprès du Load Balancer

### Terminal 3: API Gateway
```bash
./build/bin/api_gateway
```
Écoute sur `http://127.0.0.1:8080`

## Utilisation

### 1. Déployer une Fonction

**Méthode 1: Upload de fichier (RECOMMANDÉ)**
```bash
# Fonction C
curl -X POST 'http://127.0.0.1:8080/deploy?name=hello&lang=c' \
  --data-binary @examples/hello.c

# Fonction JavaScript
curl -X POST 'http://127.0.0.1:8080/deploy?name=add&lang=js' \
  --data-binary @examples/add.js

# Fonction Python
curl -X POST 'http://127.0.0.1:8080/deploy?name=greet&lang=python' \
  --data-binary @examples/greet.py
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

**Méthode 2: JSON (pour petit code)**
```bash
curl -X POST 'http://127.0.0.1:8080/deploy' \
  -H "Content-Type: application/json" \
  -d '{"name":"test","lang":"c","code":"#include <stdio.h>\nint main() { printf(\"Test\"); return 0; }"}'
```

### 2. Invoquer une Fonction

Utilisez l'ID retourné lors du déploiement:
```bash
curl -X POST 'http://127.0.0.1:8080/invoke?fn=hello_1728435600' \
  -d '{"input":"test data"}'
```

**Réponse:**
```json
{
  "ok": true,
  "output": "Hello from FAAS!\n"
}
```

### 3. Récupérer le Code d'une Fonction

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
  "code": "// Simple C function example\n#include <stdio.h>\n\nint main() {\n    printf(\"Hello from FAAS!\\\\n\");\n    return 0;\n}\n"
}
```

## Test de Charge

Utilisez l'injecteur de charge pour tester les performances:

```bash
# Déployer une fonction d'abord
curl -X POST 'http://127.0.0.1:8080/deploy?name=hello&lang=c' \
  --data-binary @examples/hello.c

# Lancer le test: 10 threads, 100 requêtes par thread
./build/bin/load_injector hello_1728435600 10 100
```

**Résultat attendu:**
```
=== Load Injector ===
Function ID: hello_1728435600
Threads: 10
Requests per thread: 100
Total requests: 1000
=====================

=== Results ===
Total requests: 1000
Successful: 1000 (100.0%)
Errors: 0 (0.0%)
Elapsed time: 2.34 seconds
Requests/sec: 427.35
===============
```

## Gestion des Erreurs

### Nom de fonction déjà existant
```bash
curl -X POST 'http://127.0.0.1:8080/deploy?name=hello&lang=c' \
  --data-binary @examples/hello.c
```
**Réponse (409 Conflict):**
```json
{
  "error": "function name 'hello' already exists, please choose another name"
}
```

### Fonction non trouvée
```bash
curl -X POST 'http://127.0.0.1:8080/invoke?fn=nonexistent'
```
**Réponse:**
```json
{
  "ok": false,
  "error": "function not found"
}
```

## Nettoyage

### Arrêter les composants
Appuyez sur `Ctrl+C` dans chaque terminal (dans l'ordre inverse):
1. API Gateway
2. Server
3. Load Balancer

### Nettoyer les sockets
```bash
rm /tmp/faas_*.sock
```

### Supprimer les fonctions déployées
```bash
rm -rf functions/
```

### Recompiler
```bash
make clean
make
```

## Structure des Fichiers

```
Hackathon IFRI/
├── build/
│   └── bin/
│       ├── api_gateway
│       ├── server
│       ├── load_balancer
│       ├── worker
│       └── load_injector
├── functions/              # Fonctions déployées
│   └── <function_id>/
│       ├── code.c/js/py
│       ├── code.wasm       # Compilé si C/Rust
│       └── metadata.json
├── examples/
│   ├── hello.c
│   ├── add.js
│   └── greet.py
├── src/
│   ├── api_gateway.c
│   ├── server.c
│   ├── load_balancer.c
│   ├── worker.c
│   ├── load_injector.c
│   ├── ipc.c
│   └── storage.c
├── include/
│   ├── ipc.h
│   └── storage.h
├── Makefile
├── README.md
└── QUICKSTART.md
```

## Dépannage

### Erreur: "server unavailable"
- Vérifiez que le Server est démarré
- Vérifiez les sockets: `ls -la /tmp/faas_*.sock`

### Erreur: "no worker available"
- Vérifiez que le Server a créé les workers
- Regardez les logs du Server et Load Balancer

### Compilation WASM échoue
- Installez clang avec wasi-sdk
- Vérifiez: `clang --target=wasm32-wasi --version`

### Worker timeout
- Augmentez le nombre de workers dans `server.c` (WORKER_POOL_SIZE)
- Vérifiez que les workers sont actifs: regardez les logs

## Langages Supportés

| Langage | Extension | Compilation WASM | Runtime | Installation |
|---------|-----------|------------------|---------|--------------|
| C       | .c        | ✅ Oui           | Wasmer  | clang + wasi-sdk |
| Rust    | .rs       | ✅ Oui           | Wasmer  | rustc + wasm32-wasi |
| Go      | .go       | ✅ Oui           | Wasmer  | tinygo |
| JavaScript | .js    | ❌ Non           | Node.js | nodejs |
| Python  | .py       | ❌ Non           | Python3 | python3 |
| PHP     | .php      | ❌ Non           | PHP CLI | php-cli |
| HTML    | .html     | ❌ Non           | Statique | Aucune |

Voir **[LANGUAGES.md](LANGUAGES.md)** pour les instructions d'installation détaillées.

## Prochaines Étapes

1. **Intégrer Wasmer**: Compiler avec `-DUSE_WASMER` et lier `libwasmer`
2. **Stratégie WEIGHTED**: Implémenter le load balancing pondéré
3. **Monitoring**: Ajouter des métriques (latence, throughput)
4. **Persistence**: Sauvegarder l'état des fonctions
5. **Auto-scaling**: Créer/détruire des workers dynamiquement
