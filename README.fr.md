[English](README.md)

# Projet Oʻahu

Reconstruction indépendante de **Test Drive Unlimited PC MC 1.66 A** en
C++ récent pour Windows x64, fondée sur le comportement du binaire officiel.
OpenTDU est une référence secondaire ; son moteur n’est pas importé ni lié.

**Projet en développement : le client complet et la coop ne sont pas encore
jouables.** Ce dépôt publie le code reconstruit, les tests et les constats
documentés. Chaque joueur devra fournir sa copie locale ; aucune ressource,
sauvegarde ou clé commerciale n’est distribuée.

## Objectif

- Reconstruire toutes les fonctions du jeu et rétablir le multijoueur complet.
- Proposer un rendu **D3D12 natif**, sans traduction D3D9, avec Vulkan au choix.
- Délier physique et IA du framerate, améliorer la boîte automatique et mesurer
  la fluidité en conduite, streaming et transitions.
- Reproduire toute la couverture manette Xbox360 sur PC avec SDL3 : Xbox et
  PlayStation, glyphes, navigation de carte et détection à chaud.
- Fournir les modes de fenêtre, le fonctionnement hors focus et la gestion VRR.
- Livrer un installateur Windows commun aux deux joueurs pour les essais coop,
  puis reconstruire les services historiques dans un serveur Rust moderne.

La [feuille de route](docs/roadmap.fr.md) résume les objectifs et les prochaines
étapes, et l'[index de la documentation](docs/README.md) renvoie à chaque
sous-système.

## État vérifié

Les lecteurs de banques, textures, modèles, scènes et matières ouvrent les
données locales en lecture seule. Les visualiseurs D3D12 natifs affichent des
ressources officielles et des assemblages explicitement choisis, avec tests
de pixels, profondeur et redimensionnement. Le monde, les matériaux complets,
le gameplay et leurs transitions restent à raccorder.

Les modules de simulation fixe, conduite, configuration, entrées et interface
sont reconstruits et testés séparément. Ces résultats de composants ne valident
pas une partie complète et jouable.

Le pont D3D9On12 du mod d’observation sert aux essais de l’ancien exécutable.
Il est distinct du [renderer natif final](docs/native_renderer_plan.md).

## Structure

| Chemin | Contenu |
| --- | --- |
| `include/tdur/`, `src/` | Ressources, jeu, physique, entrées et interface |
| `src/render/d3d12/` | Backend D3D12 natif et visualiseurs |
| `tests/` | Scénarios C++ et fixtures synthétiques |
| `mod/` | Instrumentation Win32 et lanceur d’observation isolé |
| `docs/evidence/` | Comportements établis, provenance et limites |
| `tools/` | Empaquetage des versions ; ressources commerciales exclues |

## Construire et tester

Prérequis : CMake3.25+, Visual Studio Build Tools2022 avec C++ et SDK Windows,
Python3 pour les scénarios de test. Le premier configure récupère
SDL3 depuis la release officielle épinglée et vérifie son SHA256.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
.\build\Release\tdur_inspect_game.exe --game-dir "D:\Games\Test Drive Unlimited"
```

C++23 est le minimum ; nos cibles MSVC emploient `/std:c++latest /W4 /WX`.
`TDUR_BUILD_SDL_GAMEPADS=OFF` permet une construction sans le transport SDL.
`TDUR_BUILD_D3D12_DEVICE_TESTS=OFF` désactive les témoins graphiques autonomes.
Les [dépendances](docs/dependencies.md) décrivent les versions et licences.
Les scénarios nécessitant une copie locale doivent recevoir explicitement
leurs chemins ; ils ne font pas partie d’un simple clone du dépôt.

## Téléchargements et feuille de route

Les outils de développement relus sont publiés avec leurs limites dans
[téléchargements](docs/downloads.fr.md). La [feuille de route](docs/roadmap.fr.md)
présente les prochaines étapes. Si le projet vous intéresse, une étoile sur
GitHub aide sa visibilité.

## Contribuer

Suivre le [guide de contribution](CONTRIBUTING.fr.md) et les
[lots par sous-système](docs/development_batches.md). Les preuves indiquent
le binaire concerné, l’adresse, les bornes et les limites. Une égalité d’octets
ne prouve ni l’exécution d’une routine ni le fonctionnement du jeu complet.
Protection, activation et données de référence restent inchangées.

La première publication est une sélection auditée du chantier local, avec un
historique public indépendant. Les captures
commerciales ne sont pas incluses. Les licences tierces
présentes, dont celle de Detours, sont conservées. Le code propre au projet est
publié sous [licence MIT](LICENSE), copyright Anniiki et les contributeurs de Projet Oʻahu.
