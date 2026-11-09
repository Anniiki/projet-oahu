# Feuille de route

[English](roadmap.md) | [Accueil](../README.fr.md)

La première priorité est d'entrer de manière fiable dans une vraie partie hors
ligne, puis d'assurer toute la navigation à la manette et d'intégrer les systèmes
reconstruits. Des tests de composants et un menu visible ne prouvent pas la jouabilité.

Le client final vise C++ récent, x64, D3D12 natif avec Vulkan au choix, simulation
à pas fixe, boîte automatique adaptative, améliorations d'affichage et couverture
manette Xbox/PlayStation complète avec SDL3 récent.

La modernisation visuelle et de la conduite suivra, une fois le client reconstruit
jouable. Le rendu D3D12 natif reste l'étape de la reconstruction ; le moteur
graphique a pour cible finale Unreal Engine 5. Cette étape vise aussi une physique
des véhicules moderne, des routes plus lisses ainsi qu'un ciel et un éclairage
modernisés. Ces objectifs n'ont pas de date et ne changent pas les priorités ci-dessus.

La restauration en ligne vise toute l'expérience multijoueur originale. Les
essais coop sur deux PC et l'installeur constituent des jalons de validation
distincts. Un serveur Rust ultérieur visera le comportement en ligne original
avec une architecture moderne.

Les releases publiques contiennent uniquement des livrables client vérifiés et
de la documentation utilisateur, sans contenu commercial du jeu. Aucun pourcentage
d'achèvement, date de sortie jouable ou multijoueur validé n'est promis.
