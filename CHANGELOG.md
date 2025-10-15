# Changelog - Mini FaaS

## [2.0.0] - 2025-10-09

### 🎉 Ajouts Majeurs

#### Support Multi-Langages Étendu
- ✅ **Go** : Compilation WASM avec TinyGo
- ✅ **PHP** : Exécution avec PHP CLI
- ✅ **HTML** : Fichiers statiques

**Total : 7 langages supportés** (C, Rust, Go, JavaScript, Python, PHP, HTML)

#### Nouvelles Fonctionnalités
- ✅ **GET /function/:name** : Récupération du code source par nom
- ✅ **Vérification noms uniques** : Empêche les doublons lors du deploy
- ✅ **Compilation WASM automatique** : Pour C, Rust, Go au moment du deploy
- ✅ **Load injector** : Outil de test de charge multi-threadé
- ✅ **Scripts automatisés** : start_all.sh, test_system.sh

#### Architecture Améliorée
- ✅ **Pre-fork workers** : 4 workers créés au démarrage
- ✅ **Communication bidirectionnelle** : Pipes stdin/stdout
- ✅ **Enregistrement automatique** : Workers s'enregistrent auprès du LB
- ✅ **Forward intelligent** : LB → Server → Worker spécifique

### 📝 Modifications

#### API Gateway (`api_gateway.c`)
- Ajout compilation WASM pour C/Rust/Go
- Ajout endpoint GET /function/:name
- Ajout vérification noms de fonctions existantes
- Support PHP et HTML

#### Server (`server.c`)
- Réimplémentation complète avec pre-fork
- Communication via pipes bidirectionnels
- Enregistrement workers auprès du Load Balancer
- Gestion forward_to_worker depuis LB

#### Load Balancer (`load_balancer.c`)
- Ajout enregistrement dynamique des workers
- Stratégies RR et FIFO opérationnelles
- Communication via Server pour accès workers

#### Worker (`worker.c`)
- Support Go (WASM)
- Support PHP (runtime natif)
- Support HTML (statique)
- Optimisation exécution

#### Storage (`storage.c`)
- Ajout `function_name_exists()`
- Ajout `find_function_by_name()`
- Support extensions .go, .php, .html

### 📚 Documentation

#### Nouveaux Documents
- **LANGUAGES.md** : Guide complet des langages supportés
- **QUICKSTART.md** : Guide de démarrage rapide
- **ARCHITECTURE.md** : Documentation technique détaillée
- **SUMMARY.md** : Résumé du projet
- **NEXT_STEPS.md** : Instructions de test
- **CHANGELOG.md** : Ce fichier

#### Mises à Jour
- **README.md** : Instructions d'installation par langage
- Exemples pour tous les langages

### 🔧 Fichiers Créés

#### Code Source
- `src/load_injector.c` : Test de charge
- `src/api_gateway.c` : Améliorations majeures
- `src/server.c` : Réimplémentation complète
- `src/load_balancer.c` : Réimplémentation complète
- `src/worker.c` : Support nouveaux langages
- `src/storage.c` : Nouvelles fonctions

#### Scripts
- `scripts/start_all.sh` : Démarrage automatique
- `scripts/test_system.sh` : Tests automatisés

#### Exemples
- `examples/hello.c` : Exemple C
- `examples/hello.rs` : Exemple Rust
- `examples/hello.go` : Exemple Go
- `examples/add.js` : Exemple JavaScript
- `examples/greet.py` : Exemple Python
- `examples/hello.php` : Exemple PHP
- `examples/index.html` : Exemple HTML

### 🎯 Statistiques

- **Lignes de code** : ~1500 lignes C
- **Fichiers créés** : 25+
- **Langages supportés** : 7
- **Endpoints HTTP** : 3
- **Stratégies LB** : 2 (RR, FIFO)
- **Workers pré-créés** : 4

---

## [1.0.0] - 2025-10-08

### Version Initiale
- Architecture de base (API Gateway, Server, LB, Workers)
- Support C, JavaScript, Python
- Load balancing Round Robin
- Endpoint POST /deploy et POST /invoke
- Stockage fonctions avec métadonnées

---

## Roadmap Future

### Version 2.1.0 (Court Terme)
- [ ] Intégration Wasmer complète avec libwasmer
- [ ] Stratégie WEIGHTED load balancing
- [ ] Métriques et monitoring (Prometheus)
- [ ] Endpoint DELETE /function/:name

### Version 3.0.0 (Moyen Terme)
- [ ] Auto-scaling dynamique des workers
- [ ] Support TypeScript
- [ ] API de gestion complète (CRUD)
- [ ] Authentification JWT
- [ ] Rate limiting

### Version 4.0.0 (Long Terme)
- [ ] Distribution multi-machines
- [ ] Persistence PostgreSQL
- [ ] Orchestration Kubernetes
- [ ] Sandboxing avec cgroups/namespaces
- [ ] Dashboard web de monitoring
