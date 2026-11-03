[English](CONTRIBUTING.md)

# Repository Guidelines

## Structure du projet

`include/tdur/` expose les interfaces ; `src/` sépare ressources, réseau, jeu,
physique, entrées et interface. Le rendu natif est dans `src/render/d3d12/`.
`tests/` contient les validations C++ ; `mod/` l’instrumentation Win32.
`docs/evidence/` conserve les comportements établis et leurs limites.
Les ressources commerciales et les captures privées restent hors du dépôt.

## Construire et développer

Avec CMake3.25+, Visual Studio Build Tools2022 et le SDK Windows :

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

La première configuration télécharge SDL3 avec empreinte vérifiée. Consulter
`docs/dependencies.md` pour les versions et options. L’instrumentation historique
exige une configuration Win32 distincte ; elle n’est pas le client final x64.

## Style et conventions

Utiliser le C++ récent pris en charge : C++23 minimum et `/std:c++latest` sous
MSVC. Respecter `.clang-format` : quatre espaces, pas de tabulation. Nommer
les fichiers `snake_case.cpp` et `snake_case.hpp`. Séparer logique, transport,
rendu et données ; expliciter propriété, emprunts et durées de vie.

## Validation

Nommer les tests `*_test.cpp` et les enregistrer dans CTest. Tester les branches
observées, erreurs et cas limites avec des scénarios discriminants. Aucun seuil
global de couverture n’est fixé. Documenter configuration, commande et résultat.
Distinguer compilation, tests de composants et validation en jeu ; les essais
commerciaux utilisent des sauvegardes isolées et des données en lecture seule.

## Commits et revues

Garder des commits ciblés avec un sujet français `Domaine : changement`, par
exemple `Rendu : conserver les ressources en vol`. Une revue décrit le problème,
le comportement obtenu, les preuves et les contrôles effectués. Ajouter une
capture pour un changement visuel et l’issue liée lorsqu’elle existe.

Coordonner les lots par sous-système et réserver les fichiers avant écriture.
Chaque reconstruction indique son origine et ses frontières ; un service encore
inconnu reste explicite. Ne publier aucun exécutable commercial, sauvegarde,
clé, projet Ghidra ou export brut. Ne pas modifier protection ou activation.
