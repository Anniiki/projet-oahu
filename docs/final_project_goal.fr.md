# Objectif final du projet

## Client reconstruit

Reconstruire tout Test Drive Unlimited PC MC 1.66 A, toutes ses fonctions
et fonctionnalités, en C++ récent, dans un exécutable **Windows x64 natif**.
Le binaire officiel 32 bits est la référence première ; OpenTDU fournit
seulement des travaux antérieurs à inventorier. Chaque joueur doit posséder
sa copie locale pour fournir les données commerciales, ouvertes en lecture seule.

Employer le standard C++ le plus récent effectivement pris en charge par la
chaîne de compilation. Actuellement : C++23 et MSVC `/std:c++latest`, sans
revendiquer une prise en charge complète d'un standard futur.

## Résultats fonctionnels requis

- Retrouver le jeu complet : monde, véhicules, conduite, PNJ, menus, son,
  commandes, sauvegardes, missions et autres fonctions d'origine.
- Rétablir toutes les fonctions multijoueurs, puis un mode coop testé avec
  deux joueurs réels ; utiliser Rea pour les preuves et coop-anything pour
  préparer et vérifier les essais de synchronisation et de reconnexion.
- Rendre directement en D3D12 natif, sans D3D9On12 ni traduction D3D9 à
  l'exécution, et proposer Vulkan au choix.
- Séparer physique et logique/PNJ des FPS par des pas de simulation fixes ;
  conserver un comportement correct aux fréquences d'affichage élevées.
- Intégrer la boîte automatique adaptative et les améliorations recensées
  dans les travaux OpenTDU, avec validation sur le client reconstruit.
- Proposer fenêtré, sans bordure par défaut et exclusif, les meilleurs
  réglages graphiques pris en charge, la continuité hors focus, un plafond
  adapté au moniteur actif et la prise en charge vérifiée de la VRR.

## Manettes et ergonomie Xbox 360

Intégrer la dernière version stable de SDL dans le client C++ : la version
officielle vérifiée le 30/10/2026 est [SDL 3.4.18](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.18).
La version et son empreinte doivent être figées pour des builds reproductibles,
puis actualisées après vérification des nouvelles versions stables.

La première utilisation doit reprendre la disposition et les assignations
établies sur la version Xbox 360, en préservant les réglages personnalisés.
Assurer branchement, débranchement, remapping et reconnexion à chaud sans
redémarrage, reconnaissance Xbox/PlayStation, glyphes adaptés et bascule
clavier/souris ↔ manette selon l'activité réelle. Les périphériques sans
mapping connu doivent pouvoir être configurés, sans imposer une relance.

**Reproduire toute la couverture manette de TDU Xbox 360 sur PC**, avec une
manette Xbox ou PlayStation. Cela comprend le comportement des interfaces,
pas seulement les bindings : carte, déplacements, zoom, focus, sélection,
itinéraires, validation/retour, écrans secondaires, interactions, activités,
fonctions en ligne et saisies nécessaires. La validation finale doit parcourir
toutes les fonctions à la manette, sans dépendre du clavier ou de la souris.
Inclure les options de commandes, la calibration et les vibrations du donneur
lorsqu'elles sont établies ; la gestion des entrées seule ne clôt pas ces fonctions.

Analyser intégralement la version Xbox 360 comme donneuse : code, ressources,
interfaces et fonctionnalités, puis comparer avec la référence PC MC1.66A.
Les différences retenues seront réécrites en C++ récent. Conserver une matrice
fonction/écran/action/preuve360/étatPC/validation, avec toutes les inconnues
et limites de support d'analyse ouvertes jusqu'à leur résolution. Aucun
contrôle commercial, clé ou ressource originale ne doit être modifié/distribué.


## Fluidité et absence de stuttering

Viser une expérience finale sans stuttering imputable au client, avec des
temps de trame réguliers aux fréquences prises en charge. Éviter compilation
de shaders, I/O bloquantes, attentes GPU inutiles et allocations imprévisibles
sur le chemin de rendu ; préparer les programmes et charger les ressources
de façon asynchrone, avec des budgets mémoire et des cycles de vie vérifiés.

Mesurer les temps de trame CPU/GPU, percentiles, maxima et longues trames,
pendant conduite, streaming, transitions, changement de moniteur, hotplug et
sessions coop. Une moyenne de FPS, un viewer isolé ou un benchmark synthétique
ne prouve pas ce critère ; il reste requis dans le jeu final.

## Portage 32 vers 64 bits

Le client final ne doit pas dépendre de l'exécution de l'ancien moteur x86.
Les pointeurs, tailles, alignements, conventions d'appel, dépendances et
contextes de threads doivent être reconstruits pour x64. Les offsets et
identifiants 32 bits des fichiers ou du protocole gardent leur largeur
établie ; ils ne deviennent pas automatiquement des pointeurs 64 bits.

Les sondes et mods injectés dans l'exécutable officiel restent Win32 pour
observer cette référence. Leur compilation n'établit pas le portage du jeu.
De même, les modules x64 isolés, les visualiseurs et leurs tests ne suffisent
pas à déclarer le client complet ou la coop fonctionnels.

## Distribution et premier essai coop

Le livrable final doit être un **installateur Windows `.exe`** qui déploie
tous les travaux du projet. Chaque joueur fournit son installation originale ;
l'ami d'Anniiki possède déjà le jeu sur son PC. Le setup doit détecter une
base compatible, installer notre client et nos composants, puis proposer un
lancement simple avec la configuration appropriée. Conserver les données
commerciales locales, les sauvegardes et les réglages personnalisés.

Préparer une préversion installable dès qu'une vraie session de conduite
à deux peut être testée. Prévenir Anniiki à ce jalon, avec le même installateur
pour les deux PC, les fonctions disponibles, les étapes rejoindre/héberger et
les limites connues. Vérifier auparavant deux clients en jeu, leurs véhicules
mutuellement visibles et synchronisés, les transitions de session et la reprise
après déconnexion. Les outils autonomes et échanges simulés restent des
validations de composants.

Chaque paquet porte une version de client et de protocole, un manifeste
des composants et leurs empreintes. Vérifier la concordance entre joueurs,
préparer mise à jour, sauvegarde/retour arrière et désinstallation propre.
L'installateur doit fonctionner sur un PC sans outils de développement et
fournir les prérequis nécessaires. Il contient nos composants distribuables ;
les fichiers commerciaux viennent de la copie locale de chaque joueur.

Le périmètre de chaque préversion doit être explicite. Le setup final devra
déployer le client complet C++ récent x64, ses backends D3D12/Vulkan, les
manettes et toutes les fonctions exigées dans ce document.

## Phase suivante : serveur complet en Rust

Après achèvement du client et rétablissement à 100 % des fonctionnalités
multijoueurs, reconstruire le serveur TDU 1 de A à Z en **Rust**. Il doit
devenir le service en ligne de référence auquel se connectent les clients
du projet et reproduire le comportement observable des serveurs historiques.
Les services de référence nécessaires aux validations du client peuvent
précéder cette réécriture du serveur final.

Couvrir tous les services établis : profils en ligne, sessions, découverte,
amis, clubs, invitations, courses, résultats, classements, échanges et autres
fonctions originales. Les comportements encore inconnus doivent être
documentés et établis ; une réponse simulée ne vaut pas une fonction restaurée.

Moderniser l'architecture interne en conservant la sémantique du jeu :
traitement réseau efficace, synchronisation et progression cohérentes,
persistance fiable, isolation des pannes, reprise et exploitation observables.
L'ambition « Rolls Royce » demandée par Anniiki doit être évaluée par des
budgets de latence, des essais de charge, la stabilité prolongée, la résistance
aux pertes/duplications et les scénarios de déconnexion/reconnexion.
Les seuils et capacités visés seront définis avant la validation du serveur.
