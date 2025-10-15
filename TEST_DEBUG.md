# 🐛 Test de Débogage - Exécution WASM

## 🎯 Objectif

Tester l'exécution WASM avec les logs de débogage ajoutés pour identifier le problème.

---

## 📋 Étapes de Test

### 1. Arrêter le Serveur Actuel

```bash
# Dans le terminal où le serveur tourne, appuyez sur Ctrl+C
```

### 2. Redémarrer le Serveur

```bash
cd /mnt/c/Users/Dell/Documents/Hackathon\ IFRI
./start_server.sh RR
```

### 3. Déployer la Fonction (dans un nouveau terminal)

```bash
cd /mnt/c/Users/Dell/Documents/Hackathon\ IFRI

curl -X POST 'http://127.0.0.1:8080/deploy?name=hello&lang=c' \
    -H "Content-Type: text/plain" \
    --data-binary @examples/hello.c
```

**Résultat attendu** :
```json
{"ok":true,"id":"hello_XXXXXXXXXX","name":"hello","lang":"c","wasm":true}
```

### 4. Invoquer la Fonction

```bash
# Remplacez hello_XXXXXXXXXX par l'ID retourné ci-dessus
curl -X POST 'http://127.0.0.1:8080/invoke?fn=hello_XXXXXXXXXX'
```

---

## 🔍 Logs à Surveiller

### Logs Attendus lors de l'Invocation

Dans le terminal du serveur, vous devriez voir :

```
[WORKER] 📨 Received job: fn=hello_XXXXXXXXXX, payload=
[WORKER] 🚀 Calling execute_function()...
[WORKER] 🔍 Loading metadata for: hello_XXXXXXXXXX
[WORKER] ✅ Metadata loaded: lang=c
[WORKER] 🎯 Detected WASM language: c
[WORKER] ✅ WASM file found: functions/hello_XXXXXXXXXX/code.wasm
[WORKER] 🚀 Executing WASM with Wasmer...
[WORKER] 📦 Loaded WASM file: XXXX bytes
[WORKER] ✅ WASM module compiled
[WORKER] ✅ WASI environment created
[WORKER] ✅ WASI imports retrieved
[WORKER] ✅ WASM instance created
[WORKER] ✅ Found function: _start
[WORKER] 🔧 Function signature: 0 params, 0 results
[WORKER] ⚡ Executing function...
[WORKER] ✅ Execution completed successfully
[WORKER] 📤 Captured output (XX bytes)
[WORKER] ✅ Execution succeeded: Hello from FAAS!
```

---

## 🐛 Scénarios de Débogage

### Scénario 1 : Aucun Log du Worker

**Symptôme** : Pas de log `[WORKER] 📨 Received job`

**Cause** : Le worker ne reçoit pas le job

**Solution** : Vérifier que le Load Balancer envoie bien au Server qui envoie au Worker

---

### Scénario 2 : "USE_WASMER not defined"

**Symptôme** : Log `[WORKER] ❌ USE_WASMER not defined!`

**Cause** : Le worker n'a pas été compilé avec Wasmer

**Solution** :
```bash
# Vérifier que USE_WASMER est défini dans worker.c
grep "define USE_WASMER" src/worker.c

# Recompiler
make clean && make
```

---

### Scénario 3 : "WASM file not found"

**Symptôme** : Log `[WORKER] ❌ WASM file not found`

**Cause** : La compilation WASM a échoué lors du deploy

**Solution** :
```bash
# Vérifier que le fichier WASM existe
ls -la functions/hello_XXXXXXXXXX/code.wasm

# Si absent, vérifier les logs du Server lors du deploy
# Chercher : [SERVER] ✅ Compilation successful
```

---

### Scénario 4 : Erreur Wasmer

**Symptôme** : Log `[WORKER] ❌ Failed to compile wasm module`

**Cause** : Problème avec Wasmer ou le fichier WASM

**Solution** :
```bash
# Vérifier que Wasmer est installé
ls -la /usr/local/lib/libwasmer.so

# Vérifier que le worker est lié à Wasmer
ldd build/bin/worker | grep wasmer

# Tester le WASM manuellement
wasmer run functions/hello_XXXXXXXXXX/code.wasm
```

---

### Scénario 5 : Sortie Vide

**Symptôme** : Log `[WORKER] 📤 Captured output (0 bytes)`

**Cause** : Le code WASM ne produit pas de sortie ou la capture échoue

**Solution** :
```bash
# Vérifier le code source
cat functions/hello_XXXXXXXXXX/code.c

# Doit contenir printf()
# Exemple:
# #include <stdio.h>
# int main() {
#     printf("Hello from FAAS!\n");
#     return 0;
# }
```

---

## 🧪 Test Manuel du WASM

Si vous voulez tester le WASM directement :

```bash
# Aller dans le dossier de la fonction
cd functions/hello_XXXXXXXXXX

# Exécuter avec Wasmer
wasmer run code.wasm

# Résultat attendu:
# Hello from FAAS!
```

---

## 📊 Commandes de Diagnostic

### Vérifier que Wasmer est installé

```bash
# Vérifier la bibliothèque
ls -la /usr/local/lib/libwasmer.so

# Vérifier les headers
ls -la /usr/local/include/wasm.h
ls -la /usr/local/include/wasmer.h

# Vérifier le binaire wasmer
which wasmer
wasmer --version
```

### Vérifier que le Worker est lié à Wasmer

```bash
ldd build/bin/worker | grep wasmer
# Résultat attendu:
# libwasmer.so => /usr/local/lib/libwasmer.so (0x...)
```

### Vérifier les Fichiers de la Fonction

```bash
# Lister les fichiers
ls -la functions/hello_XXXXXXXXXX/

# Devrait contenir:
# - code.c (source C)
# - code.wasm (binaire WASM)
# - metadata.json (métadonnées)
```

### Vérifier le Contenu de metadata.json

```bash
cat functions/hello_XXXXXXXXXX/metadata.json

# Devrait contenir:
# {
#   "id": "hello_XXXXXXXXXX",
#   "name": "hello",
#   "language": "c",
#   "size": XXX,
#   "created_at": XXXXXXXXXX
# }
```

---

## ✅ Résultat Attendu Final

Après l'invocation, vous devriez recevoir :

```json
{
  "ok": true,
  "output": "Hello from FAAS!\n"
}
```

Et voir tous les logs de débogage dans le terminal du serveur.

---

## 🚀 Prochaines Actions

1. ✅ Recompiler (FAIT)
2. ⏳ Redémarrer le serveur
3. ⏳ Tester l'invocation
4. ⏳ Analyser les logs
5. ⏳ Identifier le problème exact

**Lancez les tests et partagez les logs complets !**
