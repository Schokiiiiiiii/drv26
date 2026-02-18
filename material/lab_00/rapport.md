# Lab00

## Général 

- Auteur: Fabien Léger
- Cours: DRV, HEIG-VD
- Date: 16 février 2026

## Exercices

### Exercice 1

#### Correction du problème

Lorsqu'on lance le logiciel `multiple_files`, le logiciel tourne à l'infini. Le problème vient du fait que first.h et
second.h s'incluent mutuellement, ce qui continue de créer des appels. On peut contrer cela en mettant nos fichiers
sous la forme suivante.

```c 
#ifndef MY_HEAD_H
#define MY_HEAD_H

// file content

#endif // MY_HEAD_H
```

Cela permet également d'éviter de redéfinir des éléments déjà définis.

### Exercice 2

#### Résultat exécution

Le résultat de l'exécution nous dit la chose suivante.

```shell
sizeof_test.c: In function ‘print_size’:
sizeof_test.c:29:22: warning: ‘sizeof’ on array function parameter ‘str_array’ will return size of ‘char *’ [-Wsizeof-array-argument]
   29 |                sizeof(str_array), sizeof(str_out));
      |                      ^
sizeof_test.c:7:22: note: declared here
    7 | void print_size(char str_array[])
      |                 ~~~~~^~~~~~~~~~~
```

#### Tableaux de taille différente

En lançant le code, on obtient diverses informations dont la taille de nos arrays.

```shell
sizeof(str_array) = 8
sizeof(str_out) = 59
```

Le problème est que str_array est vu sous la forme d'un pointeur alors que str_out est un tableau. En effet, en passant
un tableau en paramètre, celui-ci ne peut être récupéré que sous forme de pointeur et n'a donc pas les mêmes effets avec
la commande `sizeof()`.

#### Taille du vecteur my_array

On peut savoir la taille du vecteur my_array seulement en gardant la taille quelque part en mémoire. C'est pour cela que
l'on donne pour la plupart des fonctions un pointeur et une taille pour un tableau en paramètres.

S'il s'agissait d'un tableau statique, cela ne changerait rien.

#### Compilation 32-bit

```c
gcc -m32 sizeof_test.c -o sizeof_test -Wall
```

En forçant la compilation en 32 bits, on obtient des tailles d'adresses de 4 Bytes, ce qui est plutôt logique dès le
moment où on force une infrastructure 32 bits.

### Exercice 3

On remarque lors de l'affichage que le mot est inversé selon ses octets (2 caractères hexadécimaux). Ce problème peut
être résolu en parcourant la mémoire depuis la fin jusqu'au début. On sait ainsi que le système est organisé en little
endian.

### Exercice 4

Fait.

### Exercice 5

```c 
struct a {
    int b;
    char c;
};
```

Définition d'un struct nommé `a`. On peut donc écrire `struct a a1;`.

```c 
struct {
    int b;
    char c;
} a;
```

Définition d'un struct anonyme avec une variable nommée `a`.

### Exercice 6

```shell
git grep __attribute__
```

On découvre qu'il y a diverses options d'alignement avec __attribute__.