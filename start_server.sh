#!/bin/bash

# Mini FaaS - Unified Server Starter
# Starts everything in one process

echo "╔═══════════════════════════════════════════════════════╗"
echo "║         🚀 Mini FaaS - Starting Server 🚀            ║"
echo "╚═══════════════════════════════════════════════════════╝"
echo ""

# Check if binary exists
if [ ! -f "build/bin/server" ]; then
    echo "❌ Erreur: build/bin/server introuvable"
    echo "   Compilez d'abord avec: make"
    exit 1
fi

# Get strategy from args (default: RR)
STRATEGY="${1:-RR}"

echo "📊 Configuration:"
echo "  • Stratégie: $STRATEGY"
echo "  • Workers: 4"
echo ""

# Clean up old sockets
rm -f /tmp/faas_*.sock

# Start server
exec ./build/bin/server "$STRATEGY"
