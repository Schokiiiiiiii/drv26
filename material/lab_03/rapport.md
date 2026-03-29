# Lab 03 - Introduction aux drivers kernel-space

## Général

- Auteur: Fabien Léger
- Cours: DRV, HEIG-VD
- Date: 28.03.2026

## Exercices

### Exercice 1 : mknod

> Utilisez la page de manuel de la commande mknod pour en comprendre le fonctionnement. Créez ensuite un fichier virtuel
> de type caractère avec le même couple majeur/mineur que le fichier /dev/random. Qu'est-ce qui se passe lorsque vous
> lisez son contenu avec la commande cat ?

On peut trouver sur la page man la description suivante.

```shell
mknod [OPTION]... NAME TYPE [MAJOR MINOR]
```

Voici également le fichier random sur la DE1-SoC.

````shell
0 crw-rw-rw-    1 root     root        1,   8 Jan  1 00:00 random
````

On veut donc créer un fichier virtuel avec ces options.
- NAME = random2
- TYPE = c
- MAJOR = 1
- MINOR = 8

```shell
mknod random2 c 1 8
```

Lorsque je fais la commande cat random2, on fait un cat sur un device node qui a le même couple majeur/mineur que
random. Ainsi, cela fait référence au même driver et à la même fonction read() donc le comportement est identique.

### Exercice 2 : proc

> /dev/ttyUSB0, en inspectant le fichier /proc/devices, listant les périphériques block et char du système, permet de
> découvrir qu'il s'agit d'un périphérique de type caractère Retrouvez cette information dans le fichier /proc/devices.

```shell
Character devices:
#...
  5 /dev/tty
#...
```

### Exercice 3 : sysfs

> sysfs contient davantage d'informations sur le périphérique. Retrouvez-le dans l'arborescence de sysfs, en particulier
> pour ce qui concerne le nom du driver utilisé et le type de connexion. Ensuite, utilisez la commande lsmod pour
> confirmer que le driver utilisé est bien celui identifié auparavant et cherchez si d'autres modules plus génériques
> sont impliqués. (en fonction de la distribution Linux, il se peut qu'aucuns modules plus génériques ne soient
> impliqués, car ils ont été configurés en mode "built-in" lors de la compilation du kernel)

On peut retrouver l'emplacement dans notre filesystem.

```shell
cd /sys/class/tty/ttyUSB0/device
```

Depuis là, on peut trouve le nom du driver en regardant où pointe le lien symbolique.

```shell
schoki@Scarlet:/sys/class/tty/ttyUSB0/device$ ls -lsa
total 0
0 drwxr-xr-x 4 root root    0 Mar 28 14:53 .
0 drwxr-xr-x 9 root root    0 Mar 28 14:53 ..
0 lrwxrwxrwx 1 root root    0 Mar 28 14:53 driver -> ../../../../../../../bus/usb-serial/drivers/ftdi_sio
```

On comprend donc que le driver utilisé est ftdi_sio et que le type de connexion est un sériel USB. On peut faire une
commande grep avec lsmod pour regarder s'il y a des modules plus génériques.

```shell
schoki@Scarlet:/sys/class/tty/ttyUSB0/device$ lsmod | grep -E 'ftdi_sio|usbserial'
ftdi_sio               69632  0
usbserial              69632  1 ftdi_sio
```

On comprend donc que usbserial est un module avec une implémentation plus générique que ftdi_sio. Cela veut dire que
ftdi_sio dépend de usbserial.

### Exercice 4 : empty module

> Sur votre machine hôte (laptop, machine de labo), pas sur la DE1-SoC. Compilez le module empty disponible dans les
> sources de ce laboratoire. Ensuite, montez-le dans le noyau, démontez-le, et analysez les messages enregistrés dans
> les logs.

On peut faire la suite de commandes suivantes dans le dossier pour empty_module.

```shell
make
insmod empty.ko
rmmod empty
dmesg -w
```

On peut donc voir les messages envoyés par le module lorsqu'on le monte puis le démonte.

```shell
[ 7099.982806] Hello there!
[ 7160.547109] Good bye!
```

### Exercice 5 : accumulate module

On peut compiler le module pour la DE1-SoC, créer un fichier virtuel puis insérer le module avec insmod. On peut ensuite
aller vérifier que nos informations sont correctes comme aux exercices précédents.

Host
```shell
make
scp accumulate.ko root@192.168.0.2:~/
```

DE1-SoC
```shell
mknod /dev/accumulate c 97 0
chmod 666 /dev/accumulate
insmod accumulate.ko
```

Regarder les informations dans `/proc/devices`
```shell
drv-de1 ~ # cat /proc/devices
Character devices:
...
 97 accumulate
...
```

Cependant, il n'y a rien dans `/sys/class/`.

On peut ensuite lire et écrire dans notre fichier virtuel pour intéragir avec notre driver.

````shell
drv-de1 ~ # cat /dev/accumulate 
0
drv-de1 ~ # echo "1" >> /dev/accumulate
drv-de1 ~ # cat /dev/accumulate 
1
drv-de1 ~ # echo "2" >> /dev/accumulate
drv-de1 ~ # cat /dev/accumulate 
3
````

L'utilisation de ioctl se fait via le fichier qu'il faut compiler puis mettre sur la DE1-SoC.

```shell
drv-de1 ~ # ./ioctl
Usage: ./ioctl filename cmd param
  filename: string
  cmd: unsigned int
  param: unsigned int
```

La commande nous est inconnue tandis que la paramètre est 0 ou 1. On peut trouver la commande comme ceci.

```shell
tail -f /var/log/messages
Jan  1 00:27:34 drv-de1 kern.info kernel: [ 1654.393348] Acumulate ready!
Jan  1 00:27:34 drv-de1 kern.info kernel: [ 1654.396270] ioctl ACCUMULATE_CMD_RESET: 11008
Jan  1 00:27:34 drv-de1 kern.info kernel: [ 1654.400625] ioctl ACCUMULATE_CMD_CHANGE_OP: 1074014977
```

```shell
drv-de1 ~ # ./ioctl /dev/accumulate 1074014977 1
drv-de1 ~ # cat /dev/accumulate 
3
drv-de1 ~ # echo "3" >> /dev/accumulate
drv-de1 ~ # cat /dev/accumulate 
9
drv-de1 ~ # ./ioctl /dev/accumulate 1074014977 0
drv-de1 ~ # echo "3" >> /dev/accumulate
drv-de1 ~ # cat /dev/accumulate 
12
drv-de1 ~ # ./ioctl /dev/accumulate 11008 0
drv-de1 ~ # cat /dev/accumulate 
0
```

Les changements d'opérations fonctionnent. On peut passer à la multiplication puis repassers à l'addition. Faire 3x3 est
bien égal à 9. Y ajouter 3 correspond également à 12. On peut reset grâce à la commande de reset ce qui nous donne 0.

Il suffit maintenant d'enlever le mod et vérifier avec les logs.

```shell
drv-de1 ~ # rmmod accumulate
drv-de1 ~ # tail -f /var/log/messages
Jan  1 01:03:33 drv-de1 kern.info kernel: [ 3813.394198] Acumulate done!
```