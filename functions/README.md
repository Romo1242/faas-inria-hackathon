# Functions Storage

This directory stores deployed functions.

## Structure

```
functions/
├── <function_id>/
│   ├── metadata.json    # Function metadata
│   ├── code.c           # Source code (C)
│   ├── code.js          # Source code (JS)
│   ├── code.py          # Source code (Python)
│   ├── code.wasm        # Compiled WASM (if applicable)
│   └── ...
```

## Metadata format (metadata.json)

```json
{
  "id": "unique_function_id",
  "name": "my_function",
  "language": "c|js|python|rust|go",
  "entrypoint": "main",
  "created_at": "2025-10-09T01:00:00Z",
  "updated_at": "2025-10-09T01:00:00Z",
  "size": 1024,
  "hash": "sha256_hash"
}
```

## Deployment methods

### 1. HTTP (recommended)
```bash
curl -X POST http://127.0.0.1:8080/deploy \
  -F 'name=my_func' \
  -F 'lang=c' \
  -F 'code=@function.c'
```

### 2. CLI (manual)
```bash
./scripts/deploy_function.sh my_func c function.c
```
