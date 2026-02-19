# Lab00

## Général 

- Auteur: Fabien Léger
- Cours: DRV, HEIG-VD
- Date: 19 février 2026

## Exercices

### Exercice 1

#### Correction du problème

Lorsqu'on lance le logiciel `multiple_files`, le logiciel tourne à l'infini. Le problème vient du fait que first.h et
second.h s'incluent mutuellement, ce qui continue de créer des appels. On peut contrer cela en mettant nos fichiers
sous la forme suivante.

```c++
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

```shell
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

#### Stockage via struct

```c++
struct a {
    int b;
    char c;
};
```

Définition d'un struct nommé `a`. On peut donc écrire `struct a a1;`.

```c++
struct {
    int b;
    char c;
} a;
```

Définition d'un struct anonyme avec une variable nommée `a`.

#### Mémoire utilisée

La mémoire utilisée dans les deux cas est de 8 bytes. En effet, nous avons un membre de 4 bytes et un autre de 1 byte.
L'alignement se fera donc à 4 bytes. Nous avons donc 3 bytes de padding à la fin.

### Exercice 6

#### Recherche dans le kernel linux

```shell
git grep -h "__attribute__" | sort | uniq
```

En faisant des recherches extensives sur internet, on découvre que \_\_attribute__ fait partie d'une extension en GCC.

J'ai personnellement réussi à trouver ces diverses options.

```c++
__attribute__((packed))
__attribute__((aligned(8))) // 16, 
__attribute__((format(printf, 1, 2)))
__attribute__((preserve_access_index))
__attribute__((weak))
__attribute__((noinline))
__attribute__((unused))
__attribute__((common))
__attribute__((noreturn))
__attribute__((section("sec")))
__attribute__((long_call))
__attribute__((always_inline))
__attribute__((syscall_linkage))
__attribute__((cold))
__attribute__((pure))
__attribute__((const))
```

#### Résumé

On peut s'aider du site internet [gcc](https://gcc.gnu.org/onlinedocs/gcc/Function-Attributes.html).

Voici une explication de certaines de ces fonctions.

- aligned(*alignment*) - minimum alignment for the first instruction of the function
- format(*archetype*, *string-index*, *first-to-check*) - function takes printf, scanf, strftime or strfmon style
arguments that are checked against a format string. string-index specifies which argument is the format string argument
  (starting 1) and first-to-check is the number of the first argument to check against the format string
- cold - function is unlikely to be executed (optimized for size rather than speed)
- noreturn - abort/exit cannot return, function will then never return
- const - different calls with same arguments will always give same results, independently of program state
- unused - function is possibly unused so GCC does not produce any warning for it
- pure - function depends on only its arguments and the global memory, less restrictive than const

### Exercice 7

L'alignement des champs suit l'alignement du membre le plus grand en bytes. En effet, avec une structure comme celle-ci.

```c++
struct /*__attribute__(packed)*/ Car {
    short nb_wheels;
    double length;
    char name[50];
};
```

On a le tableau récapitulatif suivant.

| Membre    | Offset | `__attribute__(packed)` |
|-----------|--------|-------------------------|
| nb_wheels | 0      | 0                       |
| length    | 8      | 2                       |
| name      | 16     | 10                      |

Cela vient du fait qu'un double fait 8 bytes et donc l'alignement se faire sur 8 bytes. On obtient d'ailleurs comme
taille de structure 72 alors que 16 + 50 = 66. Là encore, l'alignement a été augmenté jusqu'à 72 pour être un multiple
de 8 bytes.

En ajoutant `__attribute__(packed)`, on obtient des adresses qui ont plus de sens et enlève ces trous. Cela étant dit,
cela baissera également la performance d'accès à la structure qui est une des raisons pour cet alignement "étrange".

Cependant, je n'ai pas eu d'avertissements à l'utilisation de `__attribute__(packed)`.

### Exercice 8

Fait.

### Exercice 9

```c++
int main() {
    volatile int i = 0;
    if (i)
        return 1;
    return 0;
}
```

| Volatile | Optimisation | Description                                                        |
|----------|--------------|--------------------------------------------------------------------|
| non      | 0            | Aucune optimization                                                |
| non      | 1            | Simplification en return immédiat                                  |
| non      | 2            | Simplification en return immédiat                                  |
| non      | 3            | Simplification en return immédiat                                  |
| oui      | 0            | Aucune optimization                                                |
| oui      | 1            | Simplification mais garde test et retourne valeur suivant résultat |
| oui      | 2            | Simplification mais garde test et retourne valeur suivant résultat |
| oui      | 3            | Simplification mais garde test et retourne valeur suivant résultat |

Probablement qu'avec une fonction plus compliquée, les niveaux d'optimisations changeraient plus de choses. Dans tous
les cas, on voit bien que volatile permet même lors d'optimisations de garder la lecture de la valeur au cas où celle-ci
avait changé.

### Exercice 10

```c++
int main() {
    uint32_t number = 100;
    printf("%d\n", number << 42);
    return EXIT_SUCCESS;
}
```

On a un warning de l'IDE et du compilateur.

On peut également ajouter une entrée utilisateur plutôt simple.

```c++
int main() {
    uint32_t number = 4;
    uint32_t shifter = 0;
    printf("Enter a number for shifter :");
    if (scanf("%d", &shifter) != 1)
        return EXIT_FAILURE;
    printf("%d\n", number << shifter);
    return EXIT_SUCCESS;
}
```

En ajoutant `-fsanitize=undefined`, on a une erreur lors du runtime. En effet, la détection n'est possible que lorsque
l'entrée utilisateur affecte directement le shift avec une valeur erronée.

On peut également trouver l'ajout de code très intéressant ci-dessous.

```asm
cmp     ebx, 31
jbe     .L4
```

En faisant notre shift, on test la valeur donnée en la comparant à 31. Si notre valeur est plus petite ou égale à 31,
alors on faut un jump qui nous amène au reste pour ensuite printf(), sinon on continue jusqu'à appeler une fonction
pour gérer l'exception.

```asm
mov     edi, OFFSET FLAT:.Lubsan_data0
call    __ubsan_handle_shift_out_of_bounds
```

## Conclusion

J'ai beaucoup apprécié ce laboratoire. J'ai un peu eu du mal avec la fonction git grep n'ayant pas les connaissances
nécessaires pour bien l'utiliser.

Je pense que cela donne un bon rappel du langage C tout en introduisant des concepts d'assembleurs. Spécifiquement,
l'exercice 10 où on découvre que notre code change avec une option de compilation gcc est génial.