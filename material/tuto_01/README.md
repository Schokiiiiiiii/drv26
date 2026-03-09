# Tuto 01

## Général

- Auteur: Fabien Léger
- Cours: DRV, HEIG-VD
- Date: 02.03.2026

## Questions

> Explorez les connexions de notre bloc et essayez de découvrir pourquoi il a été ainsi câblé.

En écrivant/lisant aux adresses mappées, on parle avec notre périphérique. Nous pouvons voir les connexions suivantes.

- altera_axi4lite_slave - semble être un bus de données pour discuter avec le périphérique
- clk_sink - il nous faut la clock afin de marche de manière synchrone
- reset_sink - afin de remettre le périphérique dans son état initial
- interrupt_sender - afin d'envoyer des interruptions vers le CPU

> Comment ai-je pu parvenir à cette valeur ? Pourquoi le designer n'a pas choisi 0xFF200000 ? Ou, encore mieux,
> 0x00000000 ?

La base est mise à 0x0000'5000. En ajoutant cela à 0xFF20'0000 qui est l'endroit usuel où les "lightweight FPGA slaves"
sont mappés depuis, on obtient bien oxFF20'5000.

> Est-il vraiment nécessaire ? Pourrait-on le faire depuis U-Boot sans booter Linux ?
> (regardez les commandes U-Boot utilisées pour booter le système avec la commande # printenv)

On a la chronologie suivante.

- FPGA vide
- Charge le bitstream
- Matériel apparaît (bus, périphériques, etc.)
- CPU peut commencer à utiliser ce matériel

L'action de charger le bitstream peut être soit faite dans le bootloader, soit après le boot complet par linux.

On a la ligne suivante lorsqu'on fait `printenv`.

```shell
bootcmd=mmc rescan; tftpboot 0xF000 zImage; tftpboot 0x00000100 socfpga.dtb; run fpgaload; run bridge_enable_handoff; run mmcboot
```

On voit la commande `run fpgaload` qu'on peut retrouver la signification pas loin.

```shell
fpgaload=fatload mmc 0:1 0x2000000 soc_system.rbf; fpga load 0 0x2000000 0x6aebd0
```

Cela semble donc bien charger le bitstream. Il sera également possible de le faire depuis U-Boot mais il reste plus
simple de booter d'abord une fois puis de relancer la board.

> Imaginons la situation suivante :
>
> value = 0x1a
> threshold = 0x1c
> increment = 0x01
> irq capture = 0x01
> 
> Qu'est-ce qui se passe avec 4 lectures consécutives ? Détaillez les valeurs de tous les registres. Ensuite, vérifiez vos réponses sur la carte.

Première lecture - augmentation simple avec value < threshold.

```shell
value = 0x1b
irq capture = 0x01
```

Seconde lecture - augmentation avec value !< threshold. On a une interruption mais irq capture est déjà à 1.

```shell
value = 0x1c
irq capture = 0x01
```

Troisième lecture - pareil que précédent

```shell
value = 0x1d
irq capture = 0x01
```

Quatrième lecture - pareil que précédent

```shell
value = 0x1e
irq capture = 0x01
```

Ce qu'il faut comprendre est que la capture n'agit que sur la première interruption. Les captures successives ne
changent pas l'état du périphérique. Pour enlever la capture, il faut reset le counter puis écrire `1` dans irq capture.