# Architecture Détaillée - Mini FaaS en C

## Vue d'Ensemble

Ce système implémente une plateforme FaaS (Function as a Service) minimaliste en C pur, utilisant les concepts de programmation système UNIX/Linux.

## Composants Principaux

### 1. API Gateway (`api_gateway.c`)

**Rôle**: Point d'entrée HTTP pour les clients

**Fonctionnalités**:
- Serveur HTTP sur port 8080
- Gère 3 endpoints:
  - `POST /deploy?name=X&lang=Y` : Déploie une fonction
  - `POST /invoke?fn=ID` : Invoque une fonction
  - `GET /function/:name` : Récupère le code source

**Flux de déploiement**:
1. Reçoit le code source (C/JS/Python)
2. Vérifie que le nom n'existe pas déjà (`function_name_exists()`)
3. Sauvegarde le code dans `functions/<id>/code.<ext>`
4. Compile en WASM si langage compilable (C/Rust)
5. Crée les métadonnées JSON
6. Retourne l'ID unique de la fonction

**Compilation WASM**:
```c
// Pour C
clang --target=wasm32-wasi -nostdlib -Wl,--no-entry -Wl,--export-all -o code.wasm code.c

// Pour Rust
rustc --target wasm32-wasi -o code.wasm code.rs
```

**Communication**: UNIX socket vers Server (`/tmp/faas_server.sock`)

---

### 2. Server (`server.c`)

**Rôle**: Gestionnaire de workers et proxy entre API Gateway et Load Balancer

**Architecture**:
```
Server (processus principal)
  │
  ├─ Worker 0 (fork)
  │   ├─ pipe_to_worker[1]   → stdin du worker
  │   └─ pipe_from_worker[0] ← stdout du worker
  │
  ├─ Worker 1 (fork)
  ├─ Worker 2 (fork)
  └─ Worker 3 (fork)
```

**Fonctionnalités**:
- **Pre-fork**: Crée 4 workers au démarrage via `fork()`
- **Enregistrement**: Enregistre chaque worker auprès du Load Balancer
- **Communication bidirectionnelle**: Utilise des pipes pour communiquer avec les workers
- **Forwarding**: Transmet les jobs du LB vers les workers spécifiques

**Processus de création de worker**:
```c
1. pipe(pipe_to)    // Créer pipe pour envoyer jobs
2. pipe(pipe_from)  // Créer pipe pour recevoir résultats
3. fork()           // Créer processus enfant
4. dup2() stdin/stdout vers les pipes
5. execl("./build/bin/worker")
6. Enregistrer auprès du Load Balancer
```

**Gestion des requêtes**:
- `type:invoke` → Forward vers Load Balancer
- `type:forward_to_worker` → Envoie job au worker spécifique via pipe

---

### 3. Load Balancer (`load_balancer.c`)

**Rôle**: Distribution intelligente des requêtes vers les workers

**Stratégies implémentées**:

1. **Round Robin (RR)**:
   ```c
   worker_idx = (rr_idx + i) % MAX_WORKERS
   rr_idx = (worker_idx + 1) % MAX_WORKERS
   ```
   - Distribue équitablement
   - Bon pour charge uniforme

2. **FIFO (First In First Out)**:
   ```c
   // Toujours choisir le premier worker disponible
   for (i = 0; i < MAX_WORKERS; i++)
       if (workers[i].active) return i;
   ```
   - Minimise le nombre de workers actifs
   - Bon pour économiser les ressources

3. **WEIGHTED** (TODO):
   - Basé sur la charge actuelle de chaque worker
   - Envoie vers le worker le moins chargé

**Flux de distribution**:
```
1. Recevoir requête invoke du Server
2. Choisir worker selon stratégie (pick_worker())
3. Créer message forward_to_worker avec worker_id
4. Envoyer au Server qui transmet au worker
5. Recevoir résultat du Server
6. Retourner au client
```

**Enregistrement des workers**:
```json
{"type":"register_worker","worker_id":0,"pid":12345}
```

---

### 4. Worker (`worker.c`)

**Rôle**: Exécution isolée des fonctions

**Modes d'exécution**:

1. **WASM via Wasmer** (C/Rust):
   ```c
   #ifdef USE_WASMER
   - Charge le fichier .wasm
   - Crée instance Wasmer
   - Appelle fonction exportée
   - Retourne résultat
   #endif
   ```

2. **Runtime natif** (JS/Python):
   ```c
   // JavaScript
   popen("node code.js")
   
   // Python
   popen("python3 code.py")
   ```

**Cycle de vie**:
```
1. Démarré par Server via fork() + execl()
2. Lit jobs depuis stdin (pipe)
3. Parse JSON: {"type":"job","fn":"id","payload":"..."}
4. Charge métadonnées de la fonction
5. Exécute selon le langage
6. Écrit résultat sur stdout (pipe)
7. Retourne à l'étape 2
```

**Isolation**:
- Processus séparé (fork)
- Communication uniquement via pipes
- Pas d'accès réseau direct
- Environnement contrôlé

---

### 5. Storage (`storage.c`)

**Rôle**: Gestion de la persistance des fonctions

**Structure des données**:
```
functions/
└── <function_id>/
    ├── code.c/js/py/rs    # Code source
    ├── code.wasm          # Binaire WASM (si compilé)
    └── metadata.json      # Métadonnées
```

**Métadonnées JSON**:
```json
{
  "id": "hello_1728435600",
  "name": "hello",
  "language": "c",
  "entrypoint": "main",
  "created_at": "1728435600",
  "size": 123
}
```

**Fonctions clés**:
- `store_function()`: Sauvegarde code + métadonnées
- `load_function()`: Charge le code source
- `load_function_metadata()`: Charge les métadonnées
- `function_name_exists()`: Vérifie unicité du nom
- `find_function_by_name()`: Recherche par nom

---

## Communication Inter-Processus (IPC)

### UNIX Sockets

**Avantages**:
- Plus rapide que TCP/IP
- Pas de surcharge réseau
- Contrôle d'accès via système de fichiers

**Sockets utilisés**:
```
/tmp/faas_lb.sock     → Load Balancer
/tmp/faas_server.sock → Server
```

### Pipes

**Utilisation**: Communication Server ↔ Workers

**Configuration**:
```c
int pipe_to_worker[2];    // [0]=read, [1]=write
int pipe_from_worker[2];  // [0]=read, [1]=write

// Dans le worker:
dup2(pipe_to_worker[0], STDIN_FILENO);
dup2(pipe_from_worker[1], STDOUT_FILENO);
```

**Avantages**:
- Bidirectionnel
- Bufferisé par le kernel
- Bloquant (synchronisation automatique)

---

## Protocole de Communication

### Format: JSON Lines

Chaque message est une ligne JSON terminée par `\n`

**Messages API Gateway → Server**:
```json
{"type":"invoke","fn":"hello_123","payload":"test data"}
```

**Messages Server → Load Balancer**:
```json
{"type":"register_worker","worker_id":0,"pid":12345}
{"type":"invoke","fn":"hello_123","payload":"test data"}
```

**Messages Load Balancer → Server**:
```json
{"type":"forward_to_worker","worker_id":2,"job":{"type":"job","fn":"hello_123","payload":"test"}}
```

**Messages Server → Worker** (via pipe):
```json
{"type":"job","fn":"hello_123","payload":"test data"}
```

**Messages Worker → Server** (via pipe):
```json
{"ok":true,"output":"Hello from FAAS!\n"}
{"ok":false,"error":"function not found"}
```

---

## Gestion des Processus

### Signaux

**SIGCHLD**: Détection de la mort d'un worker
```c
static void sigchld_handler(int sig) {
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        // Marquer worker comme inactif
        // Décrémenter compteur
    }
}
```

**SIGINT/SIGTERM**: Arrêt propre
```c
static void sigint_handler(int sig) {
    running = 0;  // Sortir de la boucle principale
}
```

### Cleanup

**Server**:
```c
for (i = 0; i < MAX_WORKERS; i++) {
    if (workers[i].active) {
        kill(workers[i].pid, SIGTERM);
        close(pipes);
    }
}
unlink(SERVER_SOCK_PATH);
```

---

## Sécurité et Isolation

### Limitations actuelles

1. **Pas de sandboxing**: Les workers peuvent accéder au système de fichiers
2. **Pas de limites de ressources**: Pas de cgroups/ulimit
3. **Pas d'authentification**: API ouverte

### Améliorations possibles

1. **Sandboxing avec Wasmer**: WASI limite l'accès système
2. **cgroups**: Limiter CPU/mémoire par worker
3. **seccomp**: Filtrer les syscalls autorisés
4. **namespaces**: Isolation réseau/PID
5. **Authentification**: JWT/API keys

---

## Performance

### Optimisations implémentées

1. **Pre-fork**: Workers créés au démarrage (pas de latence fork)
2. **Pipes**: Communication rapide kernel-space
3. **UNIX sockets**: Pas de surcharge TCP/IP
4. **Compilation WASM**: Fait une seule fois au deploy

### Métriques attendues

- **Latence**: ~5-10ms par requête
- **Throughput**: ~400-500 req/s (4 workers)
- **Scalabilité**: Linéaire avec nombre de workers

### Goulots d'étranglement

1. **Nombre de workers fixe**: Pas d'auto-scaling
2. **Compilation WASM**: Peut être lente pour gros fichiers
3. **Sérialisation JSON**: Parsing manuel naïf

---

## Évolutions Futures

### Court terme
- [ ] Intégration Wasmer complète
- [ ] Stratégie WEIGHTED
- [ ] Métriques et monitoring

### Moyen terme
- [ ] Auto-scaling des workers
- [ ] Support Go/Rust natif
- [ ] API de gestion (list, delete functions)

### Long terme
- [ ] Distribution multi-machines
- [ ] Persistence avec base de données
- [ ] Orchestration Kubernetes
