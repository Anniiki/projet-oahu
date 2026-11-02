# Projet Oʻahu

[English](README.md)

Projet Oʻahu est un projet indépendant de modernisation du premier Test Drive
Unlimited sur PC. Chaque joueur devra posséder une copie légitime du jeu.

Ce dépôt fournit la documentation utilisateur, la feuille de route et les
téléchargements vérifiés.

## État actuel

Le développement porte encore sur les composants et leur intégration. Il
n'existe pas encore de jeu entièrement reconstruit, de coop à deux PC validée
ni d'installeur final du mod. Le diagnostic de la copie originale atteint les
menus, mais le démarrage d'une nouvelle partie hors ligne se bloque encore.

Les outils de développement disponibles inspectent une installation et ses
ressources locales, et affichent textures et maillages en D3D12 natif. Ils ne
constituent ni un jeu jouable ni un client en ligne. La construction conservée
a passé 107 tests CPU par architecture Windows x64 et Win32 ; les tests GPU
étaient exclus de la CI hébergée.

Voir les [téléchargements et limites](docs/downloads.fr.md) et la
[feuille de route](docs/roadmap.fr.md).

## Objectifs à long terme

- Reconstruire tout le jeu en C++ récent, en client PC 64 bits natif, avec le
  binaire officiel PC MC 1.66 A comme référence de comportement.
- Proposer D3D12 natif et Vulkan au choix.
- Rétablir toutes les fonctions multijoueurs originales, tester la coop sur deux
  PC et fournir un installeur aux propriétaires d'une copie légitime.
- Reproduire la couverture manette Xbox 360 complète avec SDL3 récent, connexion
  à chaud, reconnaissance Xbox/PlayStation et affichages adaptés, menus et carte.
- Séparer la simulation à pas fixe des FPS, améliorer la boîte automatique,
  corriger des bugs et améliorer la régularité des images.
- Proposer fenêtré, plein écran sans bordure et exclusif, simulation en arrière-plan,
  plafond adapté à l'écran et rafraîchissement variable.
- Fournir ensuite un serveur Rust moderne reproduisant le comportement en ligne.

Ces points sont des objectifs de développement. Aucune date de sortie n'est
promise. Les exécutables d'origine, ressources, sauvegardes et clés ne sont pas
distribués ; protection et activation d'origine restent intactes.

Une étoile GitHub peut aider à faire connaître le projet.
