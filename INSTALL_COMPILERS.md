# Installation des Compilateurs - Mini FaaS

Ce guide explique comment installer tous les compilateurs et runtimes nécessaires pour le système FaaS dans **WSL Ubuntu 22.04**.

---

## 🔧 Installation Rapide (Tout en Une)

```bash
# Mettre à jour les paquets
sudo apt-get update

# Installer les outils de base
sudo apt-get install -y build-essential wget curl git

# Installer les runtimes natifs
sudo apt-get install -y nodejs python3 php-cli

# Installer clang pour WASM
sudo apt-get install -y clang lld
```

---

## 📦 Installation Détaillée par Langage

### 1. C → WASM (clang + wasi-sdk)

#### Étape 1 : Installer Clang
```bash
sudo apt-get update
sudo apt-get install -y clang lld

# Vérifier l'installation
clang --version
# Devrait afficher: Ubuntu clang version 14.x ou supérieur
```

#### Étape 2 : Télécharger wasi-sdk
```bash
cd ~
wget https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-20/wasi-sdk-20.0-linux.tar.gz
tar xvf wasi-sdk-20.0-linux.tar.gz
```

#### Étape 3 : Ajouter au PATH
```bash
# Temporaire (pour la session actuelle)
export PATH=$PATH:$HOME/wasi-sdk-20.0/bin

# Permanent (ajouter à ~/.bashrc)
echo 'export PATH=$PATH:$HOME/wasi-sdk-20.0/bin' >> ~/.bashrc
source ~/.bashrc
```

#### Étape 4 : Vérifier
```bash
clang --target=wasm32-wasi --version
```

---

### 2. Rust → WASM (rustc + wasm32-wasi)

#### Étape 1 : Installer Rust
```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

# Choisir l'option 1 (installation par défaut)
# Puis recharger l'environnement
source $HOME/.cargo/env
```

#### Étape 2 : Ajouter la cible WASM
```bash
rustup target add wasm32-wasi
```

#### Étape 3 : Vérifier
```bash
rustc --version
rustc --target wasm32-wasi --print target-list | grep wasm32-wasi
```

---

### 3. Go → WASM (TinyGo)

**Note**: Le compilateur Go standard ne supporte pas bien WASI. TinyGo est requis.

#### Étape 1 : Télécharger TinyGo
```bash
cd ~
wget https://github.com/tinygo-org/tinygo/releases/download/v0.30.0/tinygo_0.30.0_amd64.deb
```

#### Étape 2 : Installer
```bash
sudo dpkg -i tinygo_0.30.0_amd64.deb
```

#### Étape 3 : Vérifier
```bash
tinygo version
# Devrait afficher: tinygo version 0.30.0 linux/amd64
```

#### Dépannage
Si vous avez des erreurs de dépendances :
```bash
sudo apt-get install -f
```

---

### 4. JavaScript (Node.js)

```bash
# Installer Node.js
sudo apt-get update
sudo apt-get install -y nodejs npm

# Vérifier
node --version
npm --version
```

**Alternative (version plus récente via NodeSource):**
```bash
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt-get install -y nodejs
```

---

### 5. Python

```bash
# Python3 est généralement déjà installé sur Ubuntu
python3 --version

# Si non installé
sudo apt-get update
sudo apt-get install -y python3 python3-pip

# Vérifier
python3 --version
```

---

### 6. PHP

```bash
# Installer PHP CLI
sudo apt-get update
sudo apt-get install -y php-cli

# Vérifier
php --version
```

---

### 7. HTML

**Aucune installation requise** - Les fichiers HTML sont servis statiquement.

---

## ✅ Script d'Installation Automatique

Créez un fichier `install_all.sh` :

```bash
#!/bin/bash

echo "🚀 Installation des compilateurs pour Mini FaaS"
echo "================================================"

# Mise à jour
echo "📦 Mise à jour des paquets..."
sudo apt-get update

# Outils de base
echo "🔧 Installation des outils de base..."
sudo apt-get install -y build-essential wget curl git clang lld

# Runtimes natifs
echo "🐍 Installation des runtimes natifs..."
sudo apt-get install -y nodejs python3 php-cli

# wasi-sdk pour C
echo "🔨 Installation de wasi-sdk..."
cd ~
if [ ! -d "wasi-sdk-20.0" ]; then
    wget -q https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-20/wasi-sdk-20.0-linux.tar.gz
    tar xzf wasi-sdk-20.0-linux.tar.gz
    rm wasi-sdk-20.0-linux.tar.gz
fi

# Ajouter au PATH si pas déjà fait
if ! grep -q "wasi-sdk-20.0/bin" ~/.bashrc; then
    echo 'export PATH=$PATH:$HOME/wasi-sdk-20.0/bin' >> ~/.bashrc
fi

# Rust (optionnel - demander confirmation)
read -p "📦 Installer Rust ? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
    source $HOME/.cargo/env
    rustup target add wasm32-wasi
fi

# TinyGo (optionnel - demander confirmation)
read -p "📦 Installer TinyGo ? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    cd ~
    if [ ! -f "/usr/local/bin/tinygo" ]; then
        wget -q https://github.com/tinygo-org/tinygo/releases/download/v0.30.0/tinygo_0.30.0_amd64.deb
        sudo dpkg -i tinygo_0.30.0_amd64.deb
        rm tinygo_0.30.0_amd64.deb
    fi
fi

echo ""
echo "✅ Installation terminée !"
echo ""
echo "📋 Vérification des installations :"
echo "-----------------------------------"
clang --version | head -n1
node --version
python3 --version
php --version | head -n1

if command -v rustc &> /dev/null; then
    rustc --version
fi

if command -v tinygo &> /dev/null; then
    tinygo version
fi

echo ""
echo "⚠️  IMPORTANT: Rechargez votre shell pour appliquer les changements PATH :"
echo "   source ~/.bashrc"
```

**Utilisation:**
```bash
chmod +x install_all.sh
./install_all.sh
source ~/.bashrc
```

---

## 🧪 Vérification Complète

Après installation, testez tous les compilateurs :

```bash
# C
clang --target=wasm32-wasi --version

# Rust (si installé)
rustc --version

# Go (si installé)
tinygo version

# JavaScript
node --version

# Python
python3 --version

# PHP
php --version
```

---

## 🎯 Installation Minimale (C + Runtimes Natifs)

Si vous voulez juste tester rapidement sans Rust/Go :

```bash
# Installation minimale (5 minutes)
sudo apt-get update
sudo apt-get install -y build-essential clang lld nodejs python3 php-cli

# wasi-sdk
cd ~
wget https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-20/wasi-sdk-20.0-linux.tar.gz
tar xzf wasi-sdk-20.0-linux.tar.gz
echo 'export PATH=$PATH:$HOME/wasi-sdk-20.0/bin' >> ~/.bashrc
source ~/.bashrc
```

Cela vous permettra de tester :
- ✅ C (WASM)
- ✅ JavaScript (natif)
- ✅ Python (natif)
- ✅ PHP (natif)
- ✅ HTML (statique)

---

## 🔍 Dépannage

### Problème : `clang: not found`
```bash
sudo apt-get install clang lld
```

### Problème : `wasm32-wasi target not found`
```bash
# Vérifier que wasi-sdk est dans le PATH
echo $PATH | grep wasi-sdk

# Si non, ajouter :
export PATH=$PATH:$HOME/wasi-sdk-20.0/bin
```

### Problème : `node: not found`
```bash
sudo apt-get install nodejs npm
```

### Problème : `python3: not found`
```bash
sudo apt-get install python3
```

### Problème : `php: not found`
```bash
sudo apt-get install php-cli
```

### Problème : `tinygo: not found`
```bash
# Vérifier l'installation
which tinygo

# Réinstaller si nécessaire
sudo dpkg -i tinygo_0.30.0_amd64.deb
```

---

## 📊 Espace Disque Requis

| Composant | Taille |
|-----------|--------|
| clang + lld | ~50 MB |
| wasi-sdk | ~100 MB |
| Node.js | ~50 MB |
| Python3 | ~30 MB |
| PHP | ~20 MB |
| Rust (optionnel) | ~500 MB |
| TinyGo (optionnel) | ~100 MB |
| **Total minimal** | **~250 MB** |
| **Total complet** | **~850 MB** |

---

## ⏱️ Temps d'Installation

- **Installation minimale** : 5-10 minutes
- **Installation complète** : 15-20 minutes

---

## 🚀 Après Installation

Une fois tous les compilateurs installés :

```bash
# 1. Retourner au projet
cd /mnt/c/Users/Dell/Documents/Hackathon\ IFRI

# 2. Recompiler si nécessaire
make clean && make

# 3. Démarrer le système
./scripts/start_all.sh RR

# 4. Tester le déploiement C
curl -X POST 'http://127.0.0.1:8080/deploy?name=hello&lang=c' \
  --data-binary @examples/hello.c
```

---

## 📚 Ressources

- **wasi-sdk** : https://github.com/WebAssembly/wasi-sdk
- **Rust** : https://rustup.rs/
- **TinyGo** : https://tinygo.org/getting-started/install/linux/
- **Node.js** : https://nodejs.org/
- **WASI** : https://wasi.dev/
