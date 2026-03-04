# Tuto 01

## Général

- Auteur: Fabien Léger
- Cours: DRV, HEIG-VD
- Date: 02.03.2026

## Questions

> Explorez les connexions de notre bloc et essayez de découvrir pourquoi il a été ainsi câblé.

> Comment ai-je pu parvenir à cette valeur ? Pourquoi le designer n'a pas choisi 0xFF200000 ? Ou, encore mieux,
> 0x00000000 ?

> Est-il vraiment nécessaire ? Pourrait-on le faire depuis U-Boot sans booter Linux ?
> (regardez les commandes U-Boot utilisées pour booter le système avec la commande # printenv)

> Imaginons la situation suivante :
>
> value = 0x1a
> threshold = 0x1c
> increment = 0x01
> irq capture = 0x01
> 
> Qu'est-ce qui se passe avec 4 lectures consécutives ? Détaillez les valeurs de tous les registres. Ensuite, vérifiez vos réponses sur la carte.