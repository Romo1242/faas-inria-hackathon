#!/bin/bash
# complete_invoke_test.sh - Test complet du workflow

set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

API_URL="http://127.0.0.1:8080"

echo -e "${BLUE}╔═══════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  🧪 Test Deploy → Invoke              ║${NC}"
echo -e "${BLUE}╚═══════════════════════════════════════╝${NC}\n"

# Vérifier que le serveur répond
echo -e "${YELLOW}1️⃣  Vérification du serveur...${NC}"
if curl -s --connect-timeout 2 "$API_URL/health" > /dev/null 2>&1; then
    echo -e "${GREEN}   ✅ Serveur accessible${NC}\n"
else
    echo -e "${RED}   ❌ Serveur non accessible${NC}"
    echo "   Lancez d'abord: ./start_server.sh RR"
    exit 1
fi

# Créer une fonction simple
echo -e "${YELLOW}2️⃣  Création d'une fonction de test...${NC}"
TEMP_FILE=$(mktemp)
cat > $TEMP_FILE << 'EOF'
int add(int a, int b) {
    return a + b;
}

int main() {
    return add(21, 21);
}
EOF

echo "   Code source:"
cat $TEMP_FILE | sed 's/^/   | /'
echo ""

# Deploy
echo -e "${YELLOW}3️⃣  Déploiement de la fonction...${NC}"
DEPLOY_RESPONSE=$(curl -s -X POST "$API_URL/deploy?name=add&lang=c" \
    --data-binary @$TEMP_FILE)

echo "   Réponse brute: $DEPLOY_RESPONSE"

# Extraire l'ID
FUNC_ID=$(echo "$DEPLOY_RESPONSE" | grep -o '"id":"[^"]*"' | cut -d'"' -f4)

if [ -z "$FUNC_ID" ]; then
    echo -e "${RED}   ❌ Échec du déploiement${NC}"
    rm -f $TEMP_FILE
    exit 1
fi

echo -e "${GREEN}   ✅ Fonction déployée !${NC}"
echo "   ID: $FUNC_ID"
echo ""

# Vérifier le système de fichiers
echo -e "${YELLOW}4️⃣  Vérification du répertoire de la fonction...${NC}"
FUNC_DIR="functions/$FUNC_ID"

if [ -d "$FUNC_DIR" ]; then
    echo -e "${GREEN}   ✅ Répertoire créé: $FUNC_DIR${NC}"
    echo ""
    echo "   Contenu:"
    ls -lh "$FUNC_DIR" | sed 's/^/   | /'
    echo ""
    
    # Vérifier le WASM
    if [ -f "$FUNC_DIR/code.wasm" ]; then
        WASM_SIZE=$(stat -c%s "$FUNC_DIR/code.wasm" 2>/dev/null || stat -f%z "$FUNC_DIR/code.wasm")
        echo -e "${GREEN}   ✅ WASM compilé: $WASM_SIZE octets${NC}"
    else
        echo -e "${RED}   ❌ WASM introuvable${NC}"
    fi
    
    # Afficher les métadonnées
    if [ -f "$FUNC_DIR/metadata.json" ]; then
        echo ""
        echo "   Métadonnées:"
        cat "$FUNC_DIR/metadata.json" | python3 -m json.tool 2>/dev/null | sed 's/^/   | /' || \
            cat "$FUNC_DIR/metadata.json" | sed 's/^/   | /'
    fi
else
    echo -e "${RED}   ❌ Répertoire non créé${NC}"
fi
echo ""

# Test manuel du WASM avec wasmer CLI
echo -e "${YELLOW}5️⃣  Test manuel du WASM (avec wasmer CLI)...${NC}"
if command -v wasmer &> /dev/null; then
    if [ -f "$FUNC_DIR/code.wasm" ]; then
        echo "   Exécution:"
        timeout 2s wasmer run "$FUNC_DIR/code.wasm" 2>&1 | sed 's/^/   | /' || \
            echo -e "${YELLOW}   ⚠️  Timeout ou erreur d'exécution${NC}"
    fi
else
    echo -e "${YELLOW}   ⚠️  wasmer CLI non disponible${NC}"
fi
echo ""

# Invocation via l'API
echo -e "${YELLOW}6️⃣  Invocation de la fonction via l'API...${NC}"
echo "   Requête: POST $API_URL/invoke?fn=$FUNC_ID"
echo ""

# Utiliser timeout pour éviter de bloquer
echo "   En attente de la réponse (timeout: 5s)..."
INVOKE_RESPONSE=$(timeout 5s curl -s -X POST "$API_URL/invoke?fn=$FUNC_ID" 2>&1 || echo "TIMEOUT")

if [ "$INVOKE_RESPONSE" = "TIMEOUT" ]; then
    echo -e "${RED}   ❌ TIMEOUT - Le worker ne répond pas${NC}"
    echo ""
    echo -e "${YELLOW}   🔍 Diagnostic:${NC}"
    
    # Vérifier les workers
    WORKER_COUNT=$(pgrep -x worker 2>/dev/null | wc -l)
    echo "   - Workers actifs: $WORKER_COUNT"
    
    if [ $WORKER_COUNT -eq 0 ]; then
        echo -e "${RED}     ❌ Aucun worker actif !${NC}"
    else
        # Vérifier les pipes
        FIRST_WORKER=$(pgrep -x worker | head -1)
        echo "   - Worker PID: $FIRST_WORKER"
        
        if [ -e "/proc/$FIRST_WORKER/fd/0" ]; then
            STDIN_LINK=$(readlink /proc/$FIRST_WORKER/fd/0 2>/dev/null || echo "unknown")
            echo "   - stdin: $STDIN_LINK"
            
            if [[ "$STDIN_LINK" == *"pipe"* ]]; then
                echo -e "${YELLOW}     ⚠️  Pipes OK mais worker ne répond pas${NC}"
                echo "     → Problème dans la logique du worker"
            else
                echo -e "${RED}     ❌ stdin n'est pas un pipe${NC}"
            fi
        fi
    fi
    
    echo ""
    echo -e "${YELLOW}   💡 Solutions:${NC}"
    echo "   1. Vérifier les logs du worker: grep WORKER server.log"
    echo "   2. Ajouter des logs debug dans src/worker.c"
    echo "   3. Tester l'envoi manuel: echo '{\"func_id\":\"$FUNC_ID\"}' > /proc/\$WORKER_PID/fd/0"
    
else
    echo "   Réponse:"
    echo "$INVOKE_RESPONSE" | python3 -m json.tool 2>/dev/null | sed 's/^/   | /' || \
        echo "$INVOKE_RESPONSE" | sed 's/^/   | /'
    
    if echo "$INVOKE_RESPONSE" | grep -q '"ok":true'; then
        echo ""
        echo -e "${GREEN}   ✅ SUCCÈS - La fonction a été exécutée !${NC}"
    else
        echo ""
        echo -e "${RED}   ❌ La fonction a retourné une erreur${NC}"
    fi
fi
echo ""

# Nettoyage
rm -f $TEMP_FILE

echo -e "${BLUE}╔═══════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  📊 Résumé                            ║${NC}"
echo -e "${BLUE}╚═══════════════════════════════════════╝${NC}"
echo ""
echo "Fonction déployée: $FUNC_ID"
echo "Emplacement: $FUNC_DIR"
echo "Pour réinvoquer: curl -X POST '$API_URL/invoke?fn=$FUNC_ID'"
echo ""

# Lister toutes les fonctions
echo "Fonctions disponibles:"
ls -1d functions/*/ 2>/dev/null | sed 's|functions/||;s|/$||' | sed 's/^/  - /' || echo "  (aucune)"
