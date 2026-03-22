# Lab 02

## Général

- Auteur: Fabien Léger
- Cours: DRV, HEIG-VD
- Date: 02.03.2026

## Questions

### Pourquoi doit-on spécifier tous ces paramètres ?

```shell
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- socfpga_defconfig
```

- make - utilisation de makefile
- ARCH=arm - architecture pour la compilation croisée de type arm
- CROSS_COMPILE=arm-linux-gnueabihf - compilateur pour architecture arm sur OS linux avec glibc et l'ABI pour embarqué
avec un système dédié pour calculation des floats
- socfpga_defconfig - configuration spécifique pour la plateforme suivante

```shell
Intel / Altera SoC FPGA
Cyclone V
Arria V
```

### Pouvez-vous expliquer les lignes ci-dessous ?

```c++
#include "socfpga_cyclone5.dtsi"
#include <dt-bindings/interrupt-controller/irq.h>
#include <dt-bindings/interrupt-controller/arm-gic.h>

/ {
    model = "Terasic SoCkit";
    compatible = "terasic,socfpga-cyclone5-sockit", "altr,socfpga-cyclone5", "altr,socfpga";

    soc {
        drv2026 {
            compatible = "drv2026";
            reg = <0xff200000 0x1000>;
            interrupts = <GIC_SPI 41 IRQ_TYPE_EDGE_RISING>;
            interrupt-parent = <&intc>;
        };
    };

    (...)
```

- soc - noeud où les périphériques accédant à la mémoire mappée du system on chip appartiennent
- drv2026 - périphérique nommé drv2026
- compatible = "drv2026" - fait le lien entre le périphérique et le driver
- reg - emplacement et taille de la zone pour les registres
- interrupts - description de l'interruption (type numéro mode)
- interrupt-parent - noeud du device tree qui gère l'interruption crée

### Qu'est-ce que ces commandes font ? Est-il nécessaire de recompiler tout le noyau suite à nos changements ? Et si l'on modifie encore le Device Tree ?

Compile tout sur 6 coeurs
- noyau
- modules
- device tree
```shell
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -j6
```

Compile uniquement le noyau linux
```shell
make ... zImage
```

Compile les modules du noyau
```shell
make ... modules
```
Compile le device tree
```shell
make ... socfpga_cyclone5_sockit.dtb
```

Copie les modules compilés dans `./tmp/lib/modules`
```shell
rm ./tmp -rf
make ... INSTALL_MOD_PATH="./tmp" modules_install
```

Backup de l'ancien noyau avec device tree
```shell
cp socfpga.dtb socfpga.dtb.old
cp zImage zImage.old

# Instructions en natif linux
sudo cp /srv/tftp/socfpga.dtb /srv/tftp/socfpga.dtb.old
sudo cp /srv/tftp/zImage /srv/tftp/zImage.old
```

Installation du nouveau noyau avec device tree
```shell
cp arch/.../socfpga_cyclone5_sockit.dtb ~/tftpboot/socfpga.dtb
cp arch/.../zImage ~/tftpboot/

# Instructions en natif linux
sudo cp arch/arm/boot/dts/socfpga_cyclone5_sockit.dtb /srv/tftp/socfpga.dtb
sudo cp arch/arm/boot/zImage /srv/tftp/
```

Installation des modules dans export
```shell
cp ./tmp/lib/modules/* /export/drv/ -R
```

Non, il n'est pas nécessaire de tout recompiler. Typiquement, si on modifie uniquement le device tree, il est possible 
d'uniquement le recompiler et ensuite de le copier sur notre board.

### UIO framework

> Pourquoi une région de 4096 bytes et non pas 5000 ou 10000 ? Et pourquoi on a spécifié cette adresse ?

La plage que le driver uio0 nous donne est de uniquement 4096 bytes, on ne peut donc accéder qu'à cette partie-là. Même
chose pour l'adresse. 4096 bytes est également la taille d'une page mémoire. 0xff200000 reste l'adresse pour les
mémoires physiques de périphériques comme nous avions lors de l'exercice précédent.

> Quelles sont les différences dans le comportement de mmap() susmentionnées ?

Ici, on utilise un file descriptor sur le driver uio0 ce qui signifie qu'on commence à l'adresse 0 par rapport à ce que
le driver nous propose et une région de 4096 bytes. Cela est plus sécurisé. On ne map que les régions qui ont été
définies dans le device tree.

> Quelles sont les avantages et inconvénients des drivers user-space par rapport aux drivers kernel-space.

Le code est beaucoup plus simple et plus sécurisé. Nous n'avons pas besoin de tout mettre en place pour les échanges
avec le kernel. Cependant, il y a moins de possibilités et nous sommes limité par une framework. Pour des drivers plus
complexes, il faudrait avoir un drive dans le kernel. Les drivers Userspace sont également moins performants.

## Exercices

### Exercice 4

> Il y a au moins 3 façons pour attendre une interruption au moyen de /dev/uio0. Lesquelles ?

#### read()

Avantages
- Simple d'utilisation (read, write)
- Peu de code
- Fonctionne si on attend qu'une seule interruption

Inconvénients
- Moins flexible avec plusieurs interruptions

#### select()

Avantages
- Plusieurs interruptions possibles

Inconvénients
- Utilisation complexe
- Apparemment ne peut gérer que des fd < 1024
- Il faut réinitialiser les sets à chaque tour

#### poll()

Avantages
- Ressemble à read() donc simple d'utilisation avec des ajouts
- Pas de limite de fd comme select()
- Possibilité d'utiliser un timeout

Inconvénients
- Plus de code que read()
- Pour un seul fd, read() est préférable mais sinon est meilleur que select()