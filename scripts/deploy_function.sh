#!/bin/bash
# CLI helper to deploy a function to the FAAS system

if [ $# -lt 3 ]; then
    echo "Usage: $0 <name> <lang> <code_file>"
    echo "Example: $0 my_func c function.c"
    echo ""
    echo "Supported languages: c, js, python, rust, go"
    exit 1
fi

NAME="$1"
LANG="$2"
CODE_FILE="$3"

if [ ! -f "$CODE_FILE" ]; then
    echo "Error: code file '$CODE_FILE' not found"
    exit 1
fi

echo "Deploying function '$NAME' (lang=$LANG) from $CODE_FILE..."

# Use file upload method (supports large files)
RESPONSE=$(curl -s -X POST "http://127.0.0.1:8080/deploy?name=$NAME&lang=$LANG" \
  --data-binary "@$CODE_FILE")

echo "$RESPONSE" | jq .

# Extract function ID from response
FUNC_ID=$(echo "$RESPONSE" | jq -r '.id')

if [ "$FUNC_ID" != "null" ] && [ -n "$FUNC_ID" ]; then
    echo ""
    echo "✅ Function deployed successfully!"
    echo "Function ID: $FUNC_ID"
    echo ""
    echo "Invoke with:"
    echo "  curl -X POST 'http://127.0.0.1:8080/invoke?fn=$FUNC_ID' -d '{\"input\":\"data\"}'"
else
    echo ""
    echo "❌ Deployment failed"
fi
