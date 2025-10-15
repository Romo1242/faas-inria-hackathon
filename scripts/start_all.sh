#!/bin/bash

# Script pour démarrer tous les composants du système FaaS
# Usage: ./scripts/start_all.sh [RR|FIFO]

STRATEGY=${1:-RR}
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="$PROJECT_DIR/logs"

# Créer le dossier de logs
mkdir -p "$LOG_DIR"

# Nettoyer les anciens sockets
echo "Nettoyage des anciens sockets..."
rm -f /tmp/faas_*.sock

# Fonction pour vérifier si un processus tourne
check_process() {
    pgrep -f "$1" > /dev/null
    return $?
}

# Fonction pour arrêter tous les processus
cleanup() {
    echo ""
    echo "Arrêt de tous les composants..."
    pkill -f "build/bin/api_gateway"
    pkill -f "build/bin/server"
    pkill -f "build/bin/load_balancer"
    pkill -f "build/bin/worker"
    rm -f /tmp/faas_*.sock
    echo "Arrêt terminé."
    exit 0
}

# Capturer Ctrl+C
trap cleanup SIGINT SIGTERM

echo "========================================="
echo "  Démarrage du système Mini FaaS"
echo "========================================="
echo "Stratégie de load balancing: $STRATEGY"
echo "Logs: $LOG_DIR"
echo ""

# Vérifier que les binaires existent
if [ ! -f "$PROJECT_DIR/build/bin/load_balancer" ]; then
    echo "Erreur: Les binaires n'existent pas. Exécutez 'make' d'abord."
    exit 1
fi

# 1. Démarrer Load Balancer
echo "[1/3] Démarrage du Load Balancer ($STRATEGY)..."
"$PROJECT_DIR/build/bin/load_balancer" "$STRATEGY" > "$LOG_DIR/load_balancer.log" 2>&1 &
LB_PID=$!
sleep 1

if ! check_process "load_balancer"; then
    echo "Erreur: Le Load Balancer n'a pas démarré. Vérifiez $LOG_DIR/load_balancer.log"
    exit 1
fi
echo "   ✓ Load Balancer démarré (PID: $LB_PID)"

# 2. Démarrer Server
echo "[2/3] Démarrage du Server..."
"$PROJECT_DIR/build/bin/server" > "$LOG_DIR/server.log" 2>&1 &
SERVER_PID=$!
sleep 2

if ! check_process "build/bin/server"; then
    echo "Erreur: Le Server n'a pas démarré. Vérifiez $LOG_DIR/server.log"
    kill $LB_PID 2>/dev/null
    exit 1
fi
echo "   ✓ Server démarré (PID: $SERVER_PID)"

# 3. Démarrer API Gateway
echo "[3/3] Démarrage de l'API Gateway..."
"$PROJECT_DIR/build/bin/api_gateway" > "$LOG_DIR/api_gateway.log" 2>&1 &
API_PID=$!
sleep 1

if ! check_process "api_gateway"; then
    echo "Erreur: L'API Gateway n'a pas démarré. Vérifiez $LOG_DIR/api_gateway.log"
    kill $LB_PID $SERVER_PID 2>/dev/null
    exit 1
fi
echo "   ✓ API Gateway démarré (PID: $API_PID)"

echo ""
echo "========================================="
echo "  Système démarré avec succès!"
echo "========================================="
echo "API Gateway:    http://127.0.0.1:8080"
echo "Load Balancer:  /tmp/faas_lb.sock"
echo "Server:         /tmp/faas_server.sock"
echo ""
echo "PIDs:"
echo "  - API Gateway:    $API_PID"
echo "  - Server:         $SERVER_PID"
echo "  - Load Balancer:  $LB_PID"
echo ""
echo "Logs disponibles dans: $LOG_DIR/"
echo ""
echo "Pour tester:"
echo "  curl -X POST 'http://127.0.0.1:8080/deploy?name=hello&lang=c' --data-binary @examples/hello.c"
echo "  curl -X POST 'http://127.0.0.1:8080/invoke?fn=hello_<timestamp>'"
echo ""
echo "Appuyez sur Ctrl+C pour arrêter tous les composants..."
echo ""

# Attendre indéfiniment
wait
