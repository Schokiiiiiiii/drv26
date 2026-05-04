.. _laboratoire6:

###########################################################
Laboratoire 6 --- Développement de drivers kernel-space III
###########################################################

.. only:: html

   .. figure:: ../images/logo_drv.png
      :width: 6cm
      :align: right

.. only:: latex

   .. figure:: ../images/banner_drv.png
      :width: 16cm
      :align: center


.. raw:: latex

   \newpage

=========
Objectifs
=========

* Réaliser un driver pour un capteur sur un bus I2C
* Faire du traitement différé des interruptions

===================
Matériel nécessaire
===================

Ce laboratoire utilise le même environnement que le laboratoire précédent,
qui peut donc être réutilisé.

=========================
Gestion des interruptions
=========================

Le mécanisme de gestion des interruptions utilisé jusqu'à présent peut s'avérer
inefficace lorsqu'on a des tâches conséquentes à réaliser pour traiter une interruption.
Par exemple, si on doit traiter une grande quantité de données suite à la pression d'un
bouton, il est évident qu'on ne peut pas effectuer ces opérations dans la routine d'interruption.
Il ne faut pas oublier non plus qu'il est **interdit** d'appeler dans une routine d'interruption des fonctions
qui pourraient s'endormir.

Vous avez vu pendant le cours plusieurs options pour repousser du travail à
l'extérieur du code de la routine d'interruption --- en particulier la distinction entre
*top half* (ou *hard irq*) et *bottom half*.
Parmi ces approches, les threaded irq handlers et les workqueues sont normalement
**la** façon de gérer les interruptions, sauf dans de cas très spécifiques.
Dans le tableau ci-dessous, les différentes alternatives conseillées sont listées

+--------------------------------+-------------------------------------+
| Durée IRQ handler              | Mécanisme conséillé                 |
+================================+=====================================+
| t <= 10 us                     | juste le hard irq                   |
+--------------------------------+-------------------------------------+
| 10 us < t < 100 us             | threaded irq handlers / workqueues  |
+--------------------------------+-------------------------------------+
| t >= 100 us, code non-critique | threaded irq handlers / workqueues  |
+--------------------------------+-------------------------------------+
| t >= 100 us, code critique     | tasklets                            |
+--------------------------------+-------------------------------------+

========
Exercice
========

Dans cet exercice nous allons réaliser un driver *from-scratch* pour l'accéléromètre
qui se situe sur la DE1-SoC. **Seul le code du dernier exercice devra être rendu**.

Comme nous pouvons le voir dans la figure suivante (extraite du *DE1-SoC User Manual*),
l'accéléromètre se situe proche du SoC Cyclone V et est connecté à la partie HPS.

.. figure:: images/de1-soc.png
   :scale: 75 %
   :align: center

Il s'agit d'un capteur ADXL345 fabriqué par Analog Devices.
Il est connecté au processeur au moyen d'un bus I2C et d'une ligne d'interruption
(toujours extrait du *DE1-SoC User Manual* Sec. 3.7.7):

.. figure:: images/adxl345-bus.png
   :scale: 75 %
   :align: center

Vous trouverez le datasheet de ce capteur dans le dossier :console:`material/lab_06/`.
La documentation de la DE1-SoC se situe toujours dans le dossier :console:`material/lab_01/`.

.. note::

   Un ou plusieurs drivers existent déjà pour ce capteur dans le noyau Linux.
   Saurez-vous les trouver ? Quels frameworks sont utilisés ?

   Notre driver sera différent, mais il peut toujours être utile d'analyser des drivers
   existants pour comprendre leur fonctionnement.

Configuration de l'accéléromètre
********************************

Avant de commencer, il est nécessaire de faire quelques ajustements dans le devicetree
utilisé durant les précédents laboratoires.
Le noeud pour l'accéléromètre existe déjà mais il faut changer le numéro du bus
ainsi que le flag dans la définition de l'interruption:

.. code-block:: console

        &i2c0 {
                status = "okay";

                accel1: accelerometer@53 {
                        compatible = "adi,adxl345";
                        reg = <0x53>;

                        interrupt-parent = <&portc>;
                        interrupts = <3 IRQ_TYPE_EDGE_RISING>;
                };
        };

.. note::

   D'après ce devicetree, quelle est l'adresse du capteur sur le bus I2C ?
   Cela correspond-t-il à l'information que vous trouvez dans le datasheet
   de l'ADXL345 ?

Recompilez le devicetree et redémarrer la carte avec le nouveau DTB.
Référez vous au :ref:`laboratoire2` le cas échéant.

Nous pouvons scanner les périphériques présents sur le bus I2C0
au moyen de la commande :console:`i2cdetect -y -r 0`.
Cela permet de tester si l'accéléromètre répond à l'adresse attendue:

.. code-block:: console

        root@de1soclinux:~# i2cdetect -y -r 0
            0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
        00:          -- -- -- -- -- -- -- -- -- -- -- -- --  
        10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --  
        20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --  
        30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --  
        40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --  
        50: -- -- -- 53 -- -- -- -- -- -- -- -- -- -- -- --  
        60: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --  
        70: -- -- -- -- -- -- -- --

Si le résultat est différent, il faut reprendre depuis le début et
résoudre le problème avant de continuer.

Driver de base
**************

Nous allons réaliser un premier driver basique qui permettra à l'utilisateur de faire
une mesure d'accélération et d'obtenir le résultat.

Dans un premier temps, il est nécessaire de se familiariser plus en détail avec le datasheet du capteur,
et notamment la définition des registres.
Les registres importants pour notre premier driver sont:

* DEVID
* POWER_CTL
* DATA_FORMAT
* DATAX0 à DATAZ1

Vous avez vu en cours comment enregistrer un driver sur le bus I2C.
Voici les méthodes dont vous aurez besoin pour lire et écrire un registre avec ce capteur:

* :c:`i2c_smbus_read_byte_data()`
* :c:`i2c_smbus_write_byte_data()`

La lecture de plusieurs registres consécutifs se fait au moyen de la fonction
:c:`i2c_smbus_read_i2c_block_data()`.

Libre à vous de partir de zéro ou de vous inspirer d'un précédent laboratoire pour développer
ce driver.

.. admonition:: **Exercice 1**

   Nous voulons réaliser un driver charactère auquel correspondra le *device node*
   **/dev/adxl345**.

   Dans la fonction :c:`probe()` de votre driver, il faudra réaliser au minimum les opérations
   suivantes:

   * Lire le registre :c:`DEVID` et le comparer à la valeur donnée dans le datasheet
     pour s'assurer que l'on a affaire au bon capteur. Dans le cas contraire,
     le driver doit renvoyer l'erreur standard :c:`-ENODEV`.
   * Configurer le registre :c:`DATA_FORMAT` afin d'avoir une plage de mesure de +/-4 g.
     Les autres bits de ce registre doivent rester à 0.
   * Mettre le capteur en mode mesure au moyen du registre :c:`POWER_CTL`.

   Dans la fonction :c:`remove()` de votre driver, il faudra évidemment libérer les ressoures
   allouées par votre driver, mais pensez également à mettre le capteur en veille au moyen
   du registre :c:`POWER_CTL`. Ceci doit également être fait en cas d'erreur dans le
   :c:`probe()` pour éviter de laisser le capteur consommer pour rien.

   La lecture de **/dev/adxl345** doit renvoyer la dernière mesure effectuée par le capteur.
   Le driver doit donner la valeur mesurée pour chacun des 3 axes, convertie en *g* avec
   une résolution de 3 chiffres après la virgule. Attention, le capteur renvoie des valeurs signées !
   Et souvenez-vous de ce qu'il a été dit de l'utilisation des *float* dans un driver.

   Voici un exemple quand la carte est posée horizontalement puis verticalement:

   .. code-block:: console

        root@de1soclinux:~# cat /dev/adxl345
        X = +0.008; Y = -0.032; Z = +1.031
        root@de1soclinux:~# cat /dev/adxl345
        X = +0.008; Y = +1.008; Z = -0.064

   Pour la conversion entre les unités brutes du capteur et la valeur finale en *g*,
   une erreur de quelques pourcents (< 5%) est tolérable afin de se simplifier la vie.

   Les autres opérations sur **/dev/adxl345** sont ignorées.


Détection du tap
****************

Nous allons à présent rajouter le support pour la détection du *tap* et du *double tap*
dans notre driver. En effet, cet accéléromètre dispose d'une détection
hardware des événements de "tapotement" et il est capable de générer une interruption
sur sa ligne d'IRQ. Ceci évite de devoir lire en continu le capteur pour réaliser cette
fonction en software, ce qui épargne la bande passante du bus et réduit la consommation
d'énergie.

Pour cela, un certain nombre de registres supplémentaires seront nécessaires:

* THRESH_TAP
* DUR
* Latent
* Window
* TAP_AXES
* ACT_TAP_STATUS
* INT_ENABLE
* INT_SOURCE

En plus de la documentation de ces registres, le datasheet fourni
une description de la détection du *tap* à la page 29 dans la
section "Applications information".

.. admonition:: **Exercice 2**

   La première étape est de configurer les seuils de détection du
   *tap* et *double tap* dans la méthode :c:`probe()` du driver.
   Pour cela, il est nécessaire d'écrire les registres :c:`THRESH_TAP`, :c:`DUR`, :c:`Latent` et :c:`Window`
   avec une valeur appropriée. Référez-vous à la section "Applications information" qui vous donnera
   quelques indications sur le choix de ces valeurs. Après, libre à vous d'expérimenter afin de trouver
   les meilleures paramètres.

   Il faut ensuite implémenter et configurer la routine de gestion d'interruption de notre driver,
   afin qu'elle soit appelée lorsque le capteur active sa ligne d'interruption.
   Le numéro d'interruption est donné par le champ :c:`irq` de la :c:`struct i2c_client`
   qui est passée en paramètre à la méthode :c:`probe()`.

   Ici vous devez utiliser une threaded IRQ afin que la gestion d'interruption se fasse
   dans le contexte d'un processus.
   Dans cette fonction, il faudra lire les registres :c:`ACT_TAP_STATUS` et :c:`INT_SOURCE` comme
   expliqué dans le datasheet afin de quittancer l'interruption.
   Le handler "hard IRQ" peut être vide (ou NULL).

   Finalement vous êtes prêt à activer les interruptions dans le capteur au moyen
   des registres :c:`TAP_AXES` et :c:`INT_ENABLE`. Pour l'instant vous pouvez faire
   cette étape dans la partie :c:`probe()` du driver et sélectionner l'axe de votre choix.
   Nous rajouterons une interface sysfs dans le prochain exercice pour
   permettre de configurer notre driver depuis le userspace.

   Pour tester cette partie, le plus simple sera d'ajouter un :c:`printk()` dans
   la routine de gestion d'interruption.
   Souvenez-vous que vous pouvez également lire le fichier :console:`/proc/interrupts`
   pour voir si la ligne d'interruption est effectivement appelée lorsque vous
   "tapotez" la carte.

.. note::

   Pourquoi vous a-t-on demandé d'utiliser une routine d'interruption en contexte processus ?
   Que se passerait-il si cette routine était effectuée dans le handler "hard IRQ" et pourquoi ?
   Le plus simple est probablement de tester :-)

Interface sysfs
***************

Pour l'instant, les fonctions de notre driver sont hardcodées au moment du probe.
Nous allons ajouer une interface sysfs afin de pouvoir configurer la détection du *tap*
pendant le fonctionnement.

.. admonition:: **Exercice 3**

   Ajoutez l'interface sysfs suivante:

   * :bash:`tap_axis` est un fichier en lecture / écriture qui permet de configurer l'axe
     sur lequel nous voulons faire la détection du *tap*. Les valeurs possibles
     sont "x", "y" ou "z". Par défaut l'axe "z" doit être utilisé.

   * :bash:`tap_mode` est un fichier en lecture / écriture qui permet de configurer le
     type de détection souhaitée. Les valeurs possibles sont "off", "single", "double", "both".
     Par défaut "off" est sélectionné, c'est à dire qu'aucune interruption n'est générée
     par le capteur.

   * :bash:`tap_wait` est un fichier en lecture seule. La lecture de ce fichier est bloquante
     jusqu'au prochain événement (*single tap* et/ou *double tap* en fonction de :bash:`tap_type`).
     Le résultat de la lecture sera "single" ou "double" en fonction de l'événement qui
     a débloqué l'utilisateur.
     Pour simplifier, si un utilisateur est déjà en attente, la lecture de se fichier renverra
     immédiatement "busy".

   * :bash:`tap_count` est un fichier en lecture seule qui contient le nombre d'événements
     générés depuis l'insertion du driver.

   Voici un exemple d'utilisation de cette interface:

   .. code-block:: console

       root@de1soclinux:~# cat /sys/devices/platform/soc/ffc04000.i2c/i2c-0/0-0053/tap_axis
       z
       root@de1soclinux:~# echo "x" > /sys/devices/platform/soc/ffc04000.i2c/i2c-0/0-0053/tap_axis
       root@de1soclinux:~# cat /sys/devices/platform/soc/ffc04000.i2c/i2c-0/0-0053/tap_axis
       x
       root@de1soclinux:~# cat /sys/devices/platform/soc/ffc04000.i2c/i2c-0/0-0053/tap_mode
       off
       root@de1soclinux:~# echo "single" > /sys/devices/platform/soc/ffc04000.i2c/i2c-0/0-0053/tap_mode
       root@de1soclinux:~# cat /sys/devices/platform/soc/ffc04000.i2c/i2c-0/0-0053/tap_wait
       (...)
       single
       root@de1soclinux:~# cat /sys/devices/platform/soc/ffc04000.i2c/i2c-0/0-0053/tap_count
       1

   Attention, tous les accès concurrents potentiels aux variables doivent être
   protégés correctement. Les accès concurrents au bus I2C n'ont pas besoin
   d'être protégés car ils sont sérialisés par le *bus infrastructure*.

=========================================
Travail à rendre et critères d'évaluation
=========================================

.. include:: ../consigne_rendu.rst.inc
