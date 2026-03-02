# Lab00

## Général

- Auteur: Fabien Léger
- Cours: DRV, HEIG-VD
- Date: 19 février 2026

## Questions

### Utilité des lignes de redirection

```shell
setenv ethaddr "12:34:56:78:90:12"
setenv serverip "192.168.0.1"
setenv ipaddr "192.168.0.2"
setenv gatewayip "192.168.0.1"
setenv bootdelay "2"
setenv loadaddr "0xF000"
setenv bootcmd "mmc rescan; tftpboot ${loadaddr} zImage; tftpboot ${fdtaddr} ${fdtimage}; run fpgaload; run bridge_enable_handoff; run mmcboot"
setenv mmcboot "setenv bootargs console=ttyS0,115200 root=${mmcroot} rw rootwait ip=${ipaddr}:${serverip}:${serverip}:255.255.255.0:de1soclinux:eth0:on; bootz ${loadaddr} - ${fdtaddr}"
saveenv
```
- changement de l'adresse MAC
- on fix l'adresse du server ip à celle de la machine hôte
- on fix l'adresse du SoC
- on met la passerelle si sur un autre réseau
- on fix le delay lors du boot à 2s
- on change l'adresse RAM où l'image va être chargée et lancée
- on modifie la séquence automatique au boot
  - téléchargement du kernel via tftp
  - téléchargement du device tree
  - configuration de la FPGA
  - lancement du kernel
- on modifie les arguments passés au noyau linux
  - utilisation de la bonne ip
  - changement du gateway
  - changement du netmask
  - changement de l'interface
  - activer le réseau dès le boot
- on sauvegarde tous les changements

### Commande bootz

On utilise la commande suivante lors du lancement de la SoC.

````shell
bootz ${loadaddr} - ${fdtaddr}
````

On sait en cherchant que la 

```shell
bootz <kernel_addr> <ramdisk_addr> <fdt_addr>
```

On voit donc qu'on a trois arguments avec `loadaddr` étant l'adresse du kernel et `fdtaddr`l'adresse du device tree.

Le tiret ici sert à dire qu'il n'y a pas de disk RAM.

### Toolchain `arm-linux-gnueabifh (version 11.3.1)`

#### Significations des différents mots

- arm - Architecture cible, ici un processeur ARM (et non x86_64)
- linux - Noyau cible, ici le noyau linux
- gnueabihf
  - gnu - Utilisation de la libc GNU
  - eabi - Embedded ABI (Application Binary Interface)
  - hf - hard float (calculs flottants via Floating Point Unit sur la machine)

#### Pourquoi spécifier le numéro de version

Cela décrit la version de gcc. De version en version, de nombreuses choses changent.

- backend (optimisation)
- gestion de warnings
- code

#### Que se passe-t-il si on prend juste la dernière version

On risque qu'elle ne soit pas compatible avec les outils qu'on veut utiliser et donc soit ne fonctionne plus de la même
manière, voir que cela casse entièrement.

#### Que fait cette ligne

```shell
echo "int main(){}" | arm-linux-gnueabihf-gcc -x c - -o /tmp/toolchain_test | file /tmp/toolchain_test
```

Créer une fonction main simple puis la compile dans un fichier toolchain_test avant de regarder quel type de fichier a
été créé. On peut ainsi voir que c'est bien un fichier ARM.

#### Savez-vous interpréter ces réponses ?

````shell
/tmp/toolchain_test: ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV), dynamically linked, interpreter /lib/ld-linux-armhf.so.3, BuildID[sha1]=b3a920e814dca1c9c53e894c191645227fe75d43, for GNU/Linux 3.2.0, with debug_info, not stripped
````

On voit donc bien que la compilation pour l'infrastructure cible a marchée.

#### Que signifie 'not stripped'

Cela signifie que les symboles n'ont pas été enlevés. On peut enlever tous les symboles avec la commande `strip` ce qui
permet de réduire sa taille en enlevant toute symbolique inutile au fonctionnement du programme.

### Compilation croisée

#### Que se passerait-t-il si vous essayiez de lancer un fichier ARM sur votre PC ?

Cela ne marcherait tout simplement pas, car le fichier a été compilé pour une architecture ARM tandis que mon PC est en
x86_64. Le jeu d'instruction n'est ainsi pas le même et en plus l'OS ne laisserait pas faire.

### U-Boot

#### Est-ce que cela nous dit quelque chose sur l'endianness du MCU?

```shell
md.{b|w|l} addr length
```

Vu qu'on récupère les bits de poids faible à l'adresse la plus faible, cela signifie qu'on a la fin du mot à l'adresse
la plus basse et que nous sommes en little endian.

## Exercices

### Exercice 1

```shell
md.b 0x80008000 0x1 # 1 mot de 8 bits
80008000: 46    F
md.w 0x80008000 0x1 # 1 mot de 16 bits
80008000: 4c46    FL
md.l 0x80008000 0x1 # 1 mot de 32 bits
80008000: eb004c46    FL..
md.b 0x80008000 0x4 # 4 mots de 8 bits
80008000: 46 4c 00 eb    FL..
md.w 0x80008000 0x4 # 4 mots de 16 bits
80008000: 4c46 eb00 9000 e10f    FL......
md.l 0x80008000 0x4 # 4 mots de 32 bits
80008000: eb004c46 e10f9000 e229901a e319001f    FL........).....
```

Lecture des switchs 0-9 (seul les 10 premiers bits sont utilisés)
```shell
md 0xFF200040 1
```

J'ai personnellement eu comme réponse `0x00000048`.

Écriture des LEDS 0-9
```shell
mw 0xFF200000 0x00000048
```

Les `.{b|w|l}` semblent être optionnel et être par défaut à `.l`.

Quand on lit un élément pas aligné, on a ce résultat.

```shell
SOCFPGA_CYCLONE5 # md 0x01010101 1
01010101:data abort

    MAYBE you should read doc/README.arm-unaligned-accesses

pc : [<3ff971ac>]	   lr : [<3ff9718f>]
sp : 3ff395b0  ip : 0000000f	 fp : 00000000
r10: 00000008  r9 : 01010101	 r8 : 3ff39f60
r7 : 01010101  r6 : 00000001	 r5 : 00000004  r4 : 00000001
r3 : 01010101  r2 : 80000000	 r1 : 3ff395bc  r0 : 00000009
Flags: nZCv  IRQs on  FIQs off  Mode SVC_32
Resetting CPU ...

resetting ...
```

Il semble donc que la lecture d'une adresse non alignée a provoqué un data abort ce qui a causé un reset du CPU.

La cause est l'implémentation faite de l'accès à une donnée non alignée.

### Exercice 2

On a la décomposition suivant pour un display à 7 segments.

``` 
̣      0x01
0x20        0x02
      0x40
0x10        0x04
      0x08
```

On peut ainsi deviner que par display, il faut 1 + 4 + 8 + 32 + 64 = 0b1101101 = 0x6D. On peut distribuer selon les
adresses des displays pour obtenir.

- 0xFF200020 (4 display) - 0x6D6D6D6D
- 0xFF200030 (2 display) - 0X6D6D

En effet, il semblerait que les displays soient alignés tous les bytes

```shell
mw.l 0xFF200020 0x6D6D6D6D
mw.l 0xFF200030 0x6D6D
```

On peut donc mettre toute la logique dans des variables pour obtenir cela.

```shell
setenv hex0 0x0
setenv hex5 0x6D

setenv show0 'mw.l 0xFF200020 0x${hex0}; mw.w 0xFF200030 0x${hex0}'
setenv show5 'mw.l 0xFF200020 0x${hex5}${hex5}${hex5}${hex5}; mw.w 0xFF200030 0x${hex5}${hex5}'

setenv alt 'while true; do run show0; sleep 1; run show5; sleep 1; done'
run alt
```

### Exercice 3

Fait.

## Remarques

La DE1-SoC n'a plus marché à cause de mon adaptateur qui ne marchait plus. J'ai quand même essayé de faire au mieux les
exercices 2 et 3 pour avoir un rendu. Je les testerai en dehors de ce rendu de laboratoire une fois que la connexion
avec mon PC fonctionnera à nouveau.