# Guide des Langages Supportés

## 📋 Vue d'Ensemble

Le système Mini FaaS supporte **7 langages** avec 2 stratégies d'exécution :

1. **Compilation WASM** : C, Rust, Go → Exécution via Wasmer
2. **Runtime Natif** : JavaScript, Python, PHP → Exécution directe
3. **Statique** : HTML → Retour du contenu

---

## 🔧 Langages Compilés en WASM

### 1. C

**Installation**:
```bash
# Ubuntu/Debian
sudo apt-get install clang lld

# Télécharger wasi-sdk
wget https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-20/wasi-sdk-20.0-linux.tar.gz
tar xvf wasi-sdk-20.0-linux.tar.gz
export PATH=$PATH:$PWD/wasi-sdk-20.0/bin
```

**Exemple** (`examples/hello.c`):
```c
#include <stdio.h>

int main() {
    printf("Hello from FAAS!\n");
    return 0;
}
```

**Déploiement**:
```bash
curl -X POST 'http://127.0.0.1:8080/deploy?name=hello&lang=c' \
  --data-binary @examples/hello.c
```

**Compilation**: `clang --target=wasm32-wasi -nostdlib -Wl,--no-entry -Wl,--export-all -o code.wasm code.c`

---

### 2. Rust

**Installation**:
```bash
# Installer Rust
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source $HOME/.cargo/env

# Ajouter target wasm32-wasi
rustup target add wasm32-wasi
```

**Exemple** (`examples/hello.rs`):
```rust
fn main() {
    println!("Hello from Rust WASM!");
}
```

**Déploiement**:
```bash
curl -X POST 'http://127.0.0.1:8080/deploy?name=hello_rust&lang=rust' \
  --data-binary @examples/hello.rs
```

**Compilation**: `rustc --target wasm32-wasi -o code.wasm code.rs`

---

### 3. Go

**Installation**:
```bash
# Ubuntu/Debian
wget https://github.com/tinygo-org/tinygo/releases/download/v0.30.0/tinygo_0.30.0_amd64.deb
sudo dpkg -i tinygo_0.30.0_amd64.deb

# Vérifier
tinygo version
```

**Exemple** (`examples/hello.go`):
```go
package main

import "fmt"

func main() {
    fmt.Println("Hello from Go WASM!")
}
```

**Déploiement**:
```bash
curl -X POST 'http://127.0.0.1:8080/deploy?name=hello_go&lang=go' \
  --data-binary @examples/hello.go
```

**Compilation**: `tinygo build -target=wasi -o code.wasm code.go`

**Note**: Le compilateur Go standard ne supporte pas bien WASI. TinyGo est requis.

---

## 🐍 Langages avec Runtime Natif

### 4. JavaScript (Node.js)

**Installation**:
```bash
# Ubuntu/Debian
sudo apt-get install nodejs npm

# Vérifier
node --version
```

**Exemple** (`examples/add.js`):
```javascript
function add(a, b) {
    return a + b;
}

const result = add(5, 3);
console.log("Result:", result);
```

**Déploiement**:
```bash
curl -X POST 'http://127.0.0.1:8080/deploy?name=add&lang=js' \
  --data-binary @examples/add.js
```

**Exécution**: `node code.js`

---

### 5. Python

**Installation**:
```bash
# Ubuntu/Debian (généralement déjà installé)
sudo apt-get install python3

# Vérifier
python3 --version
```

**Exemple** (`examples/greet.py`):
```python
def greet(name):
    return f"Hello, {name}!"

if __name__ == "__main__":
    print(greet("World"))
```

**Déploiement**:
```bash
curl -X POST 'http://127.0.0.1:8080/deploy?name=greet&lang=python' \
  --data-binary @examples/greet.py
```

**Exécution**: `python3 code.py`

---

### 6. PHP

**Installation**:
```bash
# Ubuntu/Debian
sudo apt-get install php-cli

# Vérifier
php --version
```

**Exemple** (`examples/hello.php`):
```php
<?php
function greet($name) {
    return "Hello, $name from PHP!";
}

echo greet("World") . "\n";
?>
```

**Déploiement**:
```bash
curl -X POST 'http://127.0.0.1:8080/deploy?name=hello_php&lang=php' \
  --data-binary @examples/hello.php
```

**Exécution**: `php code.php`

---

## 📄 Fichiers Statiques

### 7. HTML

**Installation**: Aucune

**Exemple** (`examples/index.html`):
```html
<!DOCTYPE html>
<html>
<head>
    <title>Mini FaaS</title>
</head>
<body>
    <h1>Hello from HTML!</h1>
</body>
</html>
```

**Déploiement**:
```bash
curl -X POST 'http://127.0.0.1:8080/deploy?name=index&lang=html' \
  --data-binary @examples/index.html
```

**Exécution**: Retour direct du contenu (pas d'exécution)

---

## 📊 Tableau Comparatif

| Critère | WASM (C/Rust/Go) | Runtime (JS/Python/PHP) | Statique (HTML) |
|---------|------------------|-------------------------|-----------------|
| **Compilation** | ✅ Oui (au deploy) | ❌ Non | ❌ Non |
| **Sécurité** | ✅✅✅ Sandboxing WASI | ⚠️ Accès système | ✅✅ Aucune exécution |
| **Performance** | ✅✅ Très rapide | ✅ Rapide | ✅✅✅ Instantané |
| **Portabilité** | ✅✅✅ Binaire unique | ⚠️ Dépend runtime | ✅✅ Texte brut |
| **Complexité** | ⚠️ Compilation requise | ✅ Simple | ✅✅ Très simple |
| **Bibliothèques** | ⚠️ Limitées (WASI) | ✅✅ Accès complet | ❌ N/A |

---

## 🔐 Sécurité par Langage

### WASM (C, Rust, Go)
```
✅ Sandboxing WASI complet
✅ Pas d'accès fichiers (sauf autorisé)
✅ Pas d'accès réseau
✅ Limites mémoire strictes
✅ Isolation processus
```

### Runtime Natif (JS, Python, PHP)
```
⚠️ Accès complet au système de fichiers
⚠️ Peut faire des requêtes réseau
⚠️ Peut exécuter des commandes système
⚠️ Nécessite validation du code
✅ Isolation processus (fork)
```

### HTML
```
✅✅✅ Aucune exécution de code
✅✅✅ Sécurité maximale
```

---

## 💡 Recommandations

### Pour Production
1. **Privilégier WASM** pour C, Rust, Go
   - Sécurité maximale
   - Performance optimale
   - Portabilité garantie

2. **Sandboxer les runtimes** JS/Python/PHP avec :
   - Containers (Docker)
   - `chroot` ou `jail`
   - Limites ressources (`ulimit`, `cgroups`)
   - Filtrage syscalls (`seccomp`)

### Pour Développement/Hackathon
- ✅ L'approche actuelle est parfaite
- C/Rust/Go → WASM (sécurisé)
- JS/Python/PHP → Runtime natif (simple et rapide)
- HTML → Statique (pages web)

---

## 🚀 Exemples d'Utilisation

### Déployer Tous les Langages
```bash
# WASM
curl -X POST 'http://127.0.0.1:8080/deploy?name=hello_c&lang=c' --data-binary @examples/hello.c
curl -X POST 'http://127.0.0.1:8080/deploy?name=hello_rust&lang=rust' --data-binary @examples/hello.rs
curl -X POST 'http://127.0.0.1:8080/deploy?name=hello_go&lang=go' --data-binary @examples/hello.go

# Runtime
curl -X POST 'http://127.0.0.1:8080/deploy?name=add&lang=js' --data-binary @examples/add.js
curl -X POST 'http://127.0.0.1:8080/deploy?name=greet&lang=python' --data-binary @examples/greet.py
curl -X POST 'http://127.0.0.1:8080/deploy?name=hello_php&lang=php' --data-binary @examples/hello.php

# Statique
curl -X POST 'http://127.0.0.1:8080/deploy?name=index&lang=html' --data-binary @examples/index.html
```

### Invoquer
```bash
# Utiliser l'ID retourné lors du deploy
curl -X POST 'http://127.0.0.1:8080/invoke?fn=hello_c_<timestamp>'
curl -X POST 'http://127.0.0.1:8080/invoke?fn=add_<timestamp>'
curl -X POST 'http://127.0.0.1:8080/invoke?fn=index_<timestamp>'
```

---

## ❓ FAQ

**Q: Pourquoi JavaScript/Python ne compilent pas en WASM ?**  
R: Ce sont des langages interprétés. Compiler en WASM n'apporte pas d'avantage significatif et complique le déploiement. Les runtimes natifs sont plus simples et tout aussi rapides.

**Q: Puis-je utiliser des bibliothèques externes ?**  
R: 
- **WASM**: Limitées aux bibliothèques compatibles WASI
- **Runtime natif**: Oui, toutes les bibliothèques disponibles (npm, pip, composer)

**Q: Quel langage choisir ?**  
R:
- **Performance critique + Sécurité**: C ou Rust (WASM)
- **Développement rapide**: Python ou JavaScript
- **Pages web**: HTML
- **Scripts système**: PHP

**Q: Comment ajouter un nouveau langage ?**  
R: Modifier 3 fichiers :
1. `src/api_gateway.c` - Ajouter compilation/détection
2. `src/storage.c` - Ajouter extension de fichier
3. `src/worker.c` - Ajouter logique d'exécution

---

## 📚 Ressources

- **WASI**: https://wasi.dev/
- **Wasmer**: https://wasmer.io/
- **TinyGo**: https://tinygo.org/
- **wasi-sdk**: https://github.com/WebAssembly/wasi-sdk
- **Rust WASM**: https://rustwasm.github.io/
