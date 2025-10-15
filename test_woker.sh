#!/bin/bash
# test_worker_pipe.sh - Tester la communication avec les workers

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}╔═══════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  🔍 Test Communication Workers        ║${NC}"
echo -e "${BLUE}╚═══════════════════════════════════════╝${NC}\n"

# Trouver un worker
WORKER_PID=$(pgrep -x worker | head -1)
if [ -z "$WORKER_PID" ]; then
    echo -e "${RED}❌ Aucun worker trouvé${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Worker trouvé: PID $WORKER_PID${NC}\n"

# Trouver le parent (server)
SERVER_PID=$(ps -o ppid= -p $WORKER_PID | tr -d ' ')
echo -e "${BLUE}📦 Server parent: PID $SERVER_PID${NC}\n"

# Afficher les file descriptors du worker
echo -e "${YELLOW}📋 File descriptors du worker $WORKER_PID:${NC}"
ls -la /proc/$WORKER_PID/fd/ 2>/dev/null | head -10 || echo "Permission denied"
echo ""

# Vérifier stdin/stdout
echo -e "${YELLOW}🔗 Stdin/Stdout du worker:${NC}"
echo -n "  stdin (fd 0):  "
readlink /proc/$WORKER_PID/fd/0 2>/dev/null || echo "non accessible"
echo -n "  stdout (fd 1): "
readlink /proc/$WORKER_PID/fd/1 2>/dev/null || echo "non accessible"
echo ""

# Afficher les pipes du serveur
echo -e "${YELLOW}📋 File descriptors du serveur $SERVER_PID:${NC}"
ls -la /proc/$SERVER_PID/fd/ 2>/dev/null | grep pipe | head -10 || echo "Aucun pipe visible"
echo ""

# Test d'écriture directe (nécessite root ou owner)
echo -e "${YELLOW}🧪 Test d'écriture directe au worker...${NC}"

# Construire une requête de test
TEST_REQUEST='{"func_id":"test_123","payload":"hello"}'
echo "  Envoi: $TEST_REQUEST"

# Essayer d'écrire dans le stdin du worker
if [ -w /proc/$WORKER_PID/fd/0 ]; then
    echo "$TEST_REQUEST" > /proc/$WORKER_PID/fd/0 2>/dev/null && \
        echo -e "${GREEN}  ✅ Écriture réussie${NC}" || \
        echo -e "${RED}  ❌ Écriture échouée${NC}"
else
    echo -e "${YELLOW}  ⚠️  Pas de permission d'écriture directe${NC}"
fi
echo ""

# Vérifier si le worker lit
echo -e "${YELLOW}📊 État du worker:${NC}"
ps -o pid,ppid,stat,cmd -p $WORKER_PID
echo ""

# Vérifier avec strace (si disponible et permission)
if command -v strace &> /dev/null; then
    echo -e "${YELLOW}🔬 Monitoring du worker pendant 2 secondes...${NC}"
    timeout 2s strace -p $WORKER_PID -e trace=read,write 2>&1 | head -20 || \
        echo -e "${YELLOW}  ⚠️  strace nécessite des permissions root${NC}"
else
    echo -e "${YELLOW}  ⚠️  strace non disponible${NC}"
fi
echo ""

# Test via le serveur (requête HTTP)
echo -e "${YELLOW}🌐 Test via API HTTP...${NC}"
echo "  Déploiement d'une fonction de test..."

# Créer une fonction simple
TEST_FUNC=$(mktemp)
cat > $TEST_FUNC << 'EOF'
int main() {
    return 42;
}
EOF

DEPLOY_RESULT=$(curl -s -X POST "http://127.0.0.1:8080/deploy?name=test&lang=c" \
    --data-binary @$TEST_FUNC 2>/dev/null || echo "erreur curl")

rm -f $TEST_FUNC

if echo "$DEPLOY_RESULT" | grep -q '"ok":true'; then
    echo -e "${GREEN}  ✅ Déploiement réussi${NC}"
    
    # Extraire l'ID
    FUNC_ID=$(echo "$DEPLOY_RESULT" | grep -o '"id":"[^"]*"' | cut -d'"' -f4)
    echo "  ID: $FUNC_ID"
    
    # Tenter l'invocation
    echo ""
    echo "  Invocation de la fonction..."
    
    # Utiliser timeout pour éviter de bloquer
    INVOKE_RESULT=$(timeout 3s curl -s -X POST "http://127.0.0.1:8080/invoke?fn=$FUNC_ID" 2>/dev/null || echo "timeout")
    
    if [ "$INVOKE_RESULT" = "timeout" ]; then
        echo -e "${RED}  ❌ TIMEOUT - Le worker ne répond pas${NC}"
    elif echo "$INVOKE_RESULT" | grep -q "ok"; then
        echo -e "${GREEN}  ✅ Invocation réussie: $INVOKE_RESULT${NC}"
    else
        echo -e "${YELLOW}  ⚠️  Réponse inattendue: $INVOKE_RESULT${NC}"
    fi
else
    echo -e "${RED}  ❌ Déploiement échoué: $DEPLOY_RESULT${NC}"
fi
echo ""

# Résumé
echo -e "${BLUE}╔═══════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  📊 Résumé                            ║${NC}"
echo -e "${BLUE}╚═══════════════════════════════════════╝${NC}"
echo ""
echo "Workers actifs: $(pgrep -x worker | wc -l)"
echo "Server PID: $SERVER_PID"
echo "Architecture: Server fork() → Workers avec pipes"
echo ""

# Diagnostic
if [ -e /proc/$WORKER_PID/fd/0 ]; then
    STDIN_LINK=$(readlink /proc/$WORKER_PID/fd/0 2>/dev/null || echo "unknown")
    if [[ "$STDIN_LINK" == *"pipe"* ]]; then
        echo -e "${GREEN}✅ Les pipes semblent correctement configurés${NC}"
        echo -e "${YELLOW}⚠️  Si l'invocation timeout, le problème est dans la logique du worker${NC}"
    else
        echo -e "${RED}❌ stdin n'est pas un pipe: $STDIN_LINK${NC}"
    fi
else
    echo -e "${RED}❌ Impossible d'accéder au worker${NC}"
fi
