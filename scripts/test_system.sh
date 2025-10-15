#!/bin/bash

# Script de test complet du système FaaS

echo "========================================="
echo "  Test du Système Mini FaaS"
echo "========================================="
echo ""

API_URL="http://127.0.0.1:8080"

# Test 1: Déployer fonction C
echo "[Test 1] Déploiement fonction C..."
RESPONSE=$(curl -s -X POST "$API_URL/deploy?name=hello&lang=c" --data-binary @examples/hello.c)
echo "Réponse: $RESPONSE"

if echo "$RESPONSE" | grep -q '"ok":true'; then
    FUNC_ID=$(echo "$RESPONSE" | grep -o '"id":"[^"]*"' | cut -d'"' -f4)
    echo "✓ Fonction déployée avec ID: $FUNC_ID"
else
    echo "✗ Échec du déploiement"
    exit 1
fi
echo ""

# Test 2: Invoquer la fonction
echo "[Test 2] Invocation de la fonction..."
RESPONSE=$(curl -s -X POST "$API_URL/invoke?fn=$FUNC_ID" -d '{"test":"data"}')
echo "Réponse: $RESPONSE"

if echo "$RESPONSE" | grep -q '"ok":true'; then
    echo "✓ Fonction exécutée avec succès"
else
    echo "✗ Échec de l'invocation"
fi
echo ""

# Test 3: Récupérer le code
echo "[Test 3] Récupération du code source..."
RESPONSE=$(curl -s -X GET "$API_URL/function/hello")
echo "Réponse: $RESPONSE"

if echo "$RESPONSE" | grep -q '"ok":true'; then
    echo "✓ Code récupéré avec succès"
else
    echo "✗ Échec de la récupération"
fi
echo ""

# Test 4: Déployer fonction JavaScript
echo "[Test 4] Déploiement fonction JavaScript..."
RESPONSE=$(curl -s -X POST "$API_URL/deploy?name=add&lang=js" --data-binary @examples/add.js)
echo "Réponse: $RESPONSE"

if echo "$RESPONSE" | grep -q '"ok":true'; then
    JS_FUNC_ID=$(echo "$RESPONSE" | grep -o '"id":"[^"]*"' | cut -d'"' -f4)
    echo "✓ Fonction JS déployée avec ID: $JS_FUNC_ID"
else
    echo "✗ Échec du déploiement JS"
fi
echo ""

# Test 5: Déployer fonction Python
echo "[Test 5] Déploiement fonction Python..."
RESPONSE=$(curl -s -X POST "$API_URL/deploy?name=greet&lang=python" --data-binary @examples/greet.py)
echo "Réponse: $RESPONSE"

if echo "$RESPONSE" | grep -q '"ok":true'; then
    PY_FUNC_ID=$(echo "$RESPONSE" | grep -o '"id":"[^"]*"' | cut -d'"' -f4)
    echo "✓ Fonction Python déployée avec ID: $PY_FUNC_ID"
else
    echo "✗ Échec du déploiement Python"
fi
echo ""

# Test 6: Tester nom en double
echo "[Test 6] Test nom de fonction en double..."
RESPONSE=$(curl -s -X POST "$API_URL/deploy?name=hello&lang=c" --data-binary @examples/hello.c)
echo "Réponse: $RESPONSE"

if echo "$RESPONSE" | grep -q 'already exists'; then
    echo "✓ Détection de nom en double fonctionne"
else
    echo "✗ Détection de nom en double ne fonctionne pas"
fi
echo ""

echo "========================================="
echo "  Tests terminés!"
echo "========================================="
echo ""
echo "Fonctions déployées:"
echo "  - hello (C):      $FUNC_ID"
echo "  - add (JS):       $JS_FUNC_ID"
echo "  - greet (Python): $PY_FUNC_ID"
echo ""
echo "Pour tester la charge:"
echo "  ./build/bin/load_injector $FUNC_ID 10 100"
