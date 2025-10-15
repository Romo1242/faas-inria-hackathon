#!/bin/bash

echo "🚀 Installation des compilateurs pour Mini FaaS"
echo "================================================"
echo ""

# Mise à jour
echo "📦 Mise à jour des paquets..."
sudo apt-get update -qq

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
    echo "   Téléchargement de wasi-sdk-20.0..."
    wget -q --show-progress https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-20/wasi-sdk-20.0-linux.tar.gz
    echo "   Extraction..."
    tar xzf wasi-sdk-20.0-linux.tar.gz
    rm wasi-sdk-20.0-linux.tar.gz
    echo "   ✅ wasi-sdk installé dans ~/wasi-sdk-20.0"
else
    echo "   ✅ wasi-sdk déjà installé"
fi

# Ajouter au PATH si pas déjà fait
if ! grep -q "wasi-sdk-20.0/bin" ~/.bashrc; then
    echo 'export PATH=$PATH:$HOME/wasi-sdk-20.0/bin' >> ~/.bashrc
    echo "   ✅ PATH ajouté à ~/.bashrc"
fi

# Rust (optionnel)
echo ""
read -p "📦 Installer Rust (pour support .rs) ? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    if command -v rustc &> /dev/null; then
        echo "   ✅ Rust déjà installé"
    else
        echo "   Installation de Rust..."
        curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
        source $HOME/.cargo/env
        rustup target add wasm32-wasi
        echo "   ✅ Rust installé"
    fi
else
    echo "   ⏭️  Rust ignoré"
fi

# TinyGo (optionnel)
echo ""
read -p "📦 Installer TinyGo (pour support .go) ? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    if command -v tinygo &> /dev/null; then
        echo "   ✅ TinyGo déjà installé"
    else
        cd ~
        echo "   Téléchargement de TinyGo..."
        wget -q --show-progress https://github.com/tinygo-org/tinygo/releases/download/v0.30.0/tinygo_0.30.0_amd64.deb
        echo "   Installation..."
        sudo dpkg -i tinygo_0.30.0_amd64.deb 2>/dev/null
        sudo apt-get install -f -y
        rm tinygo_0.30.0_amd64.deb
        echo "   ✅ TinyGo installé"
    fi
else
    echo "   ⏭️  TinyGo ignoré"
fi

echo ""
echo "✅ Installation terminée !"
echo ""
echo "📋 Vérification des installations :"
echo "-----------------------------------"

# Vérifications
echo -n "Clang:   "
clang --version 2>/dev/null | head -n1 || echo "❌ Non installé"

echo -n "Node.js: "
node --version 2>/dev/null || echo "❌ Non installé"

echo -n "Python:  "
python3 --version 2>/dev/null || echo "❌ Non installé"

echo -n "PHP:     "
php --version 2>/dev/null | head -n1 || echo "❌ Non installé"

if command -v rustc &> /dev/null; then
    echo -n "Rust:    "
    rustc --version 2>/dev/null || echo "❌ Non installé"
fi

if command -v tinygo &> /dev/null; then
    echo -n "TinyGo:  "
    tinygo version 2>/dev/null || echo "❌ Non installé"
fi

echo ""
echo "⚠️  IMPORTANT: Rechargez votre shell pour appliquer les changements PATH :"
echo "   source ~/.bashrc"
echo ""
echo "🚀 Ensuite, vous pouvez compiler et démarrer le projet :"
echo "   cd /mnt/c/Users/Dell/Documents/Hackathon\\ IFRI"
echo "   make clean && make"
echo "   ./scripts/start_all.sh RR"
