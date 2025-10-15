# Déploiement sur GitHub et Linux

## 1. Pousser sur GitHub (depuis Windows)

```powershell
# Dans le dossier du projet
cd "C:\Users\Dell\Documents\Hackathon IFRI"

# Initialiser git
git init

# Ajouter tous les fichiers
git add .

# Commit
git commit -m "Initial commit: Mini FAAS in C"

# Créer un repo sur GitHub puis :
git remote add origin https://github.com/<ton_username>/<ton_repo>.git
git branch -M main
git push -u origin main
```

## 2. Récupérer sur Linux

```bash
# Cloner le repo
git clone https://github.com/<ton_username>/<ton_repo>.git
cd <ton_repo>

# Installer les dépendances
sudo apt update
sudo apt install build-essential gcc make

# (Optionnel) Pour compilation WASM
sudo apt install clang

# (Optionnel) Pour JS/Python
sudo apt install nodejs python3

# Compiler
make

# Vérifier
ls -lh build/bin/
# Tu devrais voir : api_gateway, load_balancer, server, worker
```

## 3. Lancer le système

```bash
# Terminal 1 : Load Balancer
./build/bin/load_balancer RR

# Terminal 2 : Server
./build/bin/server

# Terminal 3 : API Gateway
./build/bin/api_gateway

# Terminal 4 : Tests
curl -X POST 'http://127.0.0.1:8080/deploy?name=hello&lang=c' \
  --data-binary @examples/hello.c
```

## 4. Vérifier que ça fonctionne

```bash
# Voir les processus
ps aux | grep -E "load_balancer|server|api_gateway"

# Voir les sockets
ls -l /tmp/faas_*.sock

# Tester un déploiement
curl -X POST 'http://127.0.0.1:8080/deploy?name=test&lang=c' \
  -H "Content-Type: application/json" \
  -d '{"name":"test","lang":"c","code":"int main() { return 42; }"}'
```

## 5. Troubleshooting

### Erreur de compilation
```bash
make clean
make V=1  # Mode verbose
```

### Port 8080 déjà utilisé
```bash
sudo lsof -i :8080
# Tuer le processus ou changer le port dans src/api_gateway.c
```

### Sockets déjà existants
```bash
rm /tmp/faas_*.sock
```

### Permissions
```bash
chmod +x scripts/deploy_function.sh
chmod +x build/bin/*
```

## 6. Structure du projet

```
.
├── src/                 # Code source C
│   ├── api_gateway.c
│   ├── server.c
│   ├── load_balancer.c
│   ├── worker.c
│   ├── ipc.c
│   └── storage.c
├── include/             # Headers
│   ├── ipc.h
│   └── storage.h
├── examples/            # Fonctions exemples
│   ├── hello.c
│   ├── add.js
│   └── greet.py
├── scripts/             # Scripts helper
│   └── deploy_function.sh
├── functions/           # Stockage fonctions (créé au runtime)
├── Makefile            # Build system
├── README.md           # Documentation
└── .gitignore          # Fichiers ignorés par git
```

## 7. Développement

### Modifier le code
```bash
# Éditer un fichier
vim src/worker.c

# Recompiler
make

# Relancer
pkill -f load_balancer
pkill -f server
pkill -f api_gateway
./build/bin/load_balancer RR &
./build/bin/server &
./build/bin/api_gateway
```

### Ajouter une fonctionnalité
```bash
# Créer une branche
git checkout -b feature/nouvelle-fonctionnalite

# Modifier, tester, commit
git add .
git commit -m "Add: nouvelle fonctionnalité"

# Pousser
git push origin feature/nouvelle-fonctionnalite
```

## 8. Notes importantes

- ✅ Le code fonctionne **uniquement sur Linux** (sockets UNIX, fork, pipes)
- ✅ Sur Windows, utiliser **WSL2**
- ✅ Les workers sont créés **dynamiquement** par le Server
- ✅ Le Load Balancer **distribue** seulement (RR/FIFO)
- ✅ Wasmer est **optionnel** (stub par défaut)
