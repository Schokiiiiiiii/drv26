.. _laboratoire2:

####################################
Laboratoire 2 --- User-space Drivers
####################################

.. only:: latex

   .. figure:: ../images/banner_drv.png
      :width: 16cm
      :align: center

Le but de ce laboratoire est de vous présenter les drivers
user-space. Il s'agit d'une étape intermédiaire entre le monde user-space (que vous avez
exploré dans le :ref:`laboratoire1`) et le monde kernel-space
(qui sera le sujet du reste du cours).

Il n'est pas toujours nécessaire d'écrire un driver kernel-space, et opérer en
user-space présente des avantages intéressants (mais aussi de fortes limitations).
Dans ce laboratoire on explorera les deux.

.. raw:: latex

    \newpage

=========
Objectifs
=========

* Apprendre à effectuer des changements simples dans la configuration du noyau.
* Comprendre les différentes approches relatives aux drivers user-space.
* Investiguer les avantages/désavantages par rapport aux drivers kernel-space.

===================
Matériel nécessaire
===================

Dans ce laboratoire on mettra les mains dans le noyau !
Il nous faudra donc le télécharger.
On utilisera la version 6.1.55-lts du noyau Linux, mais on choisira une version non-mainline (c.-à-d., pas celle "officielle").
Ci-dessous, vous verrez comment récupérer le noyau, le configurer, et le compiler.


===============================================================================
Téléchargement du noyau Linux et ajout des modules pour le User-space I/O (UIO)
===============================================================================

Comme mentionné ci-dessus, nous utiliserons une version du noyau non-mainline,
c.-à-d., la version customisée par une entité tierce (dans notre cas, Altera).
Vous aviez déjà cloné le dépôt git lors du :ref:`laboratoire0`.
Vérifiez que la branche de votre clone locale soit :bash:`socfpga-6.1.55-lts` qui est la version du noyau que nous utiliserons :

.. code-block:: console

    $ git branch --show-current
    socfpga-6.1.55-lts

Si ce n'est pas la bonne branche et comme l'option :bash:`--depth=1` avait été utilisée pour limiter la taille du clone,
le changement de branche *classique* n'est pas possible.
Le plus simple est de recloner le dépôt avec la bonne branche :

.. code-block:: console

    $ rm linux-socfpga -r
    $ git clone --depth=1 -b socfpga-6.1.55-lts https://github.com/altera-opensource/linux-socfpga.git
    $ cd linux-socfpga

Lorsqu'on doit configurer le noyau pour une nouvelle carte, ce n'est pas nécessaire de démarrer
de zéro à chaque fois.
En fait, des configurations de base ont été déjà préparées pour les cartes les plus connues.
Vous pouvez donc importer directement cette configuration comme point de départ :

.. code-block:: console

    $ make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- socfpga_defconfig

.. note::

    Pourquoi doit-on spécifier tous ces paramètres ?

    Au besoin, si :file:`arm-linux-gnueabihf-` n'est pas dans les :file:`PATH`, il est possible
    de définir son chemin complet pour le paramètre :file:`CROSS_COMPILE`.

Cette commande remplacera la configuration actuelle (contenue dans le fichier :file:`.config`) avec
une configuration adaptée à la carte.

.. warning::

    Cela veut aussi dire que, lorsqu'on veut jouer un peu avec la configuration sans trop de
    risques, il est sage de faire une copie du fichier :file:`.config` **avant** de la bricoler...
    même si automatiquement le *menuconfig* (qu'on verra ci-dessous) fait une copie
    de l'ancienne configuration (fichier :file:`.config.old`) à la sauvegarde.

Toutefois, cette configuration ne contient pas les options qui pourraient nous intéresser,
dans le cas spécifique de ce laboratoire les *userspace drivers* ne sont pas actifs.
Vous pouvez le voir avec la commande :

.. code-block:: console

    $ grep UIO .config

(*UIO = Userspace I/O*, vous trouvez l'howto officiel `ici <https://www.kernel.org/doc/html/latest/driver-api/uio-howto.html>`__).

Vous pouvez modifier la configuration avec :

.. code-block:: console

    $ make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- menuconfig

Dans l'interface ncurses, vous pouvez chercher les éléments d'intérêt avec la touche :c:`/`.
Tapez *UIO* dans le champ de recherche, et avec les numéros sélectionnez le résultat qui vous
intéresse. Dans le cas de :numref:`menuconfig_search`, il s'agit du numéro *"1"*.

.. _menuconfig_search:
.. figure:: images/menuconfig_search.png
   :width: 800px
   :align: center

   Résultats de la recherche.

Avec la touche barre espace vous pouvez commuter entre :c:`< >` (désélectionné), :c:`<M>` (module),
et :c:`<*>` (inclus dans le noyau).
Choisissez de le compiler en tant que **module**.
Avec la touche entrée vous pouvez voir le sous-menu (:numref:`menuconfig_submenu`).

.. _menuconfig_submenu:
.. figure:: images/menuconfig_submenu.png
   :width: 800px
   :align: center

   Sub-menu UIO.

Ici sélectionnez *Userspace I/O platform driver with generic IRQ handling* pour
la compilation en tant que module.

Pour sortir du menu de configuration, avec les flèches et la touche entrée
choisissez *Exit* dans la barre en bas jusqu'au moment où le système vous demande
si vous voulez sauvegarder la nouvelle configuration.
Appuyez sur entrée pour le faire, et vous devriez être au nouveau de retour à la
ligne de commande.

Vérifiez que la nouvelle configuration a bien été sauvegardée :

.. code-block:: console

    $ grep UIO .config
    CONFIG_UIO=m
    # CONFIG_UIO_CIF is not set
    CONFIG_UIO_PDRV_GENIRQ=m
    # CONFIG_UIO_DMEM_GENIRQ is not set
    [...]

Il faut ensuite modifier le device tree pour qu'on puisse gérer les boutons avec le UIO driver.
Le device tree est une structure de donnée qui décrit les composants hardware (DRAM, UART, USB, bus I2C, ...) d'une carte
et permet au kernel de connaître et utiliser correctement ces différents composants,
notamment les différentes adresses mémoires, numéros d'interruption ou encore le nombre de coeur CPU disponible.
Ainsi, il n'y a pas besoin de hardcodé toutes ces informations dans le noyau de l'OS !
Plus d'information sur les devices trees sont disponibles dans
`ces slides <https://bootlin.com/pub/conferences/2021/webinar/petazzoni-device-tree-101/petazzoni-device-tree-101.pdf>`__.


Ouvrez dans un éditeur de texte le fichier :file:`arch/arm/boot/dts/socfpga_cyclone5_sockit.dts`
et ajoutez, juste après la ligne qui contient le :c:`#include` (ligne 6) les deux lignes suivantes, pour permettre l'utilisation de certain :c:`define` :

.. code-block:: c

    #include <dt-bindings/interrupt-controller/irq.h>
    #include <dt-bindings/interrupt-controller/arm-gic.h>

Ajoutez également, juste après la ligne qui contient le *compatible* (ligne 10-12), le bloc :

.. code-block:: c

    soc {
        drv2026 {
            compatible = "drv2026";
            reg = <0xff200000 0x1000>;
            interrupts = <GIC_SPI 41 IRQ_TYPE_EDGE_RISING>;
            interrupt-parent = <&intc>;
        };
    };

Le résultat final devrait donc ressembler à:

.. code-block:: c

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


.. note::

    Pouvez-vous expliquer les lignes ci-dessus ?

On peut maintenant compiler le noyau, les modules et le device tree.
(vous pouvez démarrer la compilation et ensuite lire le reste du texte, cela va
prendre un moment...)

.. code-block:: bash

    # Compilation du noyau, des modules et du devicetree
    # -jN permet de lancer N compilations en parallèles
    # (adaptez N au nombre de coeurs de votre PC)
    $ make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -j6

    # On pourrait aussi compiler uniquement le noyau:
    # make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- zImage
    # Ou les modules:
    # make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules
    # Ou le devicetree:
    # make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- socfpga_cyclone5_sockit.dtb

    # Copie des modules dans le sous-répertoire tmp/
    $ rm ./tmp -rf
    $ make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- INSTALL_MOD_PATH="./tmp" modules_install

    # Backup de l'ancien noyau/DT et copie des nouveaux fichiers
    $ cp $HOME/tftpboot/socfpga.dtb $HOME/tftpboot/socfpga.dtb.old
    $ cp $HOME/tftpboot/zImage $HOME/tftpboot/zImage.old
    $ cp arch/arm/boot/dts/socfpga_cyclone5_sockit.dtb $HOME/tftpboot/socfpga.dtb
    $ cp arch/arm/boot/zImage $HOME/tftpboot/
    $ cp ./tmp/lib/modules/* /export/drv/ -R

.. note::

    En effet on aurait dû copier les modules déjà pour le :ref:`laboratoire1`,
    lorsqu'on a utilisé le nouveau noyau (qui se trouvait dans l'archive fournie),
    mais on ne les utilisait pas donc on a (sciemment) sauté une étape...

.. note::

    Qu'est-ce que ces commandes font ? Est-il nécessaire de recompiler tout
    le noyau suite à nos changements ? Et si l'on modifie encore le Device Tree ?

Sur la carte :

.. code-block:: console

    drv-de1:~# mkdir /lib/modules
    drv-de1:~# cd /lib/modules
    drv-de1:/lib/modules# mv /root/drv/6.1.55* .

Redémarrez la carte.

Maintenant le noyau devrait être configuré avec les drivers user-space disponibles comme
modules.
Vérifiez que ce soit effectivement le cas en tapant :

.. code-block:: console

   drv-de1:~# zcat /proc/config.gz | grep UIO

.. note::

    Le noyau Linux contient des millions d'options configurables.
    N'hésitez pas à explorer :c:`menuconfig` pour vous faire une idée de ce qui est disponible.

Le driver :bash:`uio_pdrv_genirq` ne devrait pas être chargé au démarrage,
comme le montre la sortie vide de la commande :bash:`lsmod`.

=====================================================
Accès aux périphériques en utilisant :file:`/dev/mem`
=====================================================

Dans le :ref:`laboratoire1` vous avez vu comment accéder à des
dispositifs memory-mapped en utilisant :file:`/dev/mem`.
En se basant sur l':ref:`exercice 3 <lab1-ex3>` de ce dernier laboratoire,
réalisez le programme suivant (toujours en utilisant :c:`mmap` et :file:`/dev/mem` pour l'instant).

.. admonition:: **Exercice 1**

    Ecrivez un logiciel user-space en C qui réalise une éditeur de texte simpliste.
    Cet éditeur est limité à l'édition d'un texte de 6 caractères en majuscules,
    qui sera affiché sur les afficheurs 7-segments.

    Au démarrage, rien n'est affiché sur les afficheurs 7-segments (soit le texte "      ").
    L'utilisateur peut éditer le texte affiché, caractère par caractère, de la manière suivante:

      - En appuyant sur KEY0, le caractère actuellement édité est décrémenté
          * ``Z`` devient ``Y``, ... , ``B`` devient ``A``
          * Si ``A`` est affiché, le caractère est effacé (devient " ")
      - En appuyant sur KEY1, le caractère actuellement édité est incrémenté
          * ``A`` devient ``B``, ... , ``Y`` devient ``Z``
          * Si ``Z`` est déjà affiché, rien ne se passe
      - En appuyant sur KEY2, le "curseur" d'édition est déplacé vers la droite
          * Soit HEX5 → HEX4 → ... → HEX0
      - En appuyant sur KEY3, le "curseur" d'édition est déplacé vers la gauche
          * Soit HEX5 ← HEX4 ← ... ← HEX0

    A vous de choisir le caractère qui est en cours d'édition au démarrage du programme (HEX0 ou HEX5 ou... ?).

    De plus, la LED correspondant au caractère actuellement édité doit être allumée.

      - Exemple: si HEX5 est en cours d'édition, la LED5 est allumée

    Eteignez les afficheurs 7-segments et les LEDs en partant
    (quand le programme est arrêté avec Ctrl+C) pour éviter de faire fondre la banquise
    inutilement.

    **Note**: vous pouvez trouver un exemple de police de caractère pour afficheurs
    7-segments sur cette page Wikipedia:
    https://en.wikipedia.org/wiki/Seven-segment_display_character_representations


=====================================================
Accès aux périphériques en utilisant le UIO framework
=====================================================

L'approche utilisée au point précédent présente plusieurs soucis, notamment au
niveau de la sécurité du système.
Le Userspace I/O framework résout ce type de problème en permettant un accès
plus restreint à la mémoire.
En effet, il est possible de cibler (dans le device tree) exactement la région
de mémoire qu'on veut exposer à l'utilisateur.

Le device tree que vous avez bricolé fait cela -- il offre
au système l'accès à une région de 4096 bytes à partir de l'adresse
*0xff200000*.

Attention, le comportement de :c:`mmap()`, dans le cas de UIO, est en partie
différent (voir par exemple
`ici <https://www.osadl.org/fileadmin/dam/rtlws/12/Koch.pdf>`__).

Vous pouvez trouver de plus amples informations sur UIO dans le UIO HOW-TO
(disponible dans la documentation du noyau Linux
`ici <https://www.kernel.org/doc/html/latest/driver-api/uio-howto.html>`__).
Seules les sections
`How UIO works <https://www.kernel.org/doc/html/latest/driver-api/uio-howto.html#how-uio-works>`__
et `Writing a driver in userspace <https://www.kernel.org/doc/html/latest/driver-api/uio-howto.html#writing-a-driver-in-userspace>`__
vous seront utiles pour ce que nous faisons ici.

.. important::
    Pour pouvoir configurer le driver générique du UIO (uio_pdrv_genirq)
    correctement il faut qu'il soit chargé avec la bonne valeur du paramètre
    of_id.

    La commande suivante s'assure que le module soit déchargé et l'insère,
    en choisissant le dispositif qui doit être contrôlé.

    **Cette commande doit être exécutée après chaque boot pour utiliser les UIO !**

    .. code-block:: console

        drv-de1:~# modprobe -r uio_pdrv_genirq; modprobe uio_pdrv_genirq of_id="drv2026"

.. note::

    * Pourquoi une région de 4096 bytes et non pas 5000 ou 10000 ? Et
      pourquoi on a spécifié cette adresse ?
    * Quelles sont les différences dans le comportement de :c:`mmap()`
      susmentionnées ?
    * Quelles sont les avantages et inconvénients des drivers user-space par rapport
      aux drivers kernel-space.

.. admonition:: **Exercice 2**

    Ecrivez un driver user-space pour réaliser la même tâche que
    l'exercice 1 (mais gardez une copie de l'exercice 1).
    Vous pouvez prendre exemple sur le tutoriel disponible
    `ici <https://yurovsky.github.io/2014/10/10/linux-uio-gpio-interrupt.html>`__.

    Pas besoin de gérer les interruptions pour cet exercice.

==============================
UIO framework et interruptions
==============================

Un autre très grand avantage du UIO framework par rapport à l'accès à travers
:file:`/dev/mem` est la possibilité de gérer les interruptions (bien que d'une
façon un peu "primitive").

.. admonition:: **Exercice 3**

    À l'aide du tutoriel disponible
    `ici <https://yurovsky.github.io/2014/10/10/linux-uio-gpio-interrupt.html>`__,
    améliorez le programme de l'exercice précédent (toujours en gardant une copie de celui-ci)
    afin de gérer l'appui sur les boutons au moyen d'interruptions.

    .. warning::

        Les interruptions **doivent** être activées et le registre
        **doit** être nettoyé après chaque interruption...
        Voir section 3.4 du document *DE1-SoC Computer System with ARM Cortex-A9* !!

    .. hint::

        Il faut d'abord être sûrs que les interruptions soient bien reçues.
        Le fichier :file:`/proc/interrupts` pourra vous aider.
        Vous pouvez par exemple utiliser la commande

        .. code-block:: console

            drv-de1:~# cat /proc/interrupts

        pour observer les interruptions reçues par le système.
        Utilisez cette commande juste avant et juste après avoir lancer votre programme
        (ou un programme plus simple de test) et appuyé sur des boutons.
        Votre programme doit au moins activer les interruptions correctement pour qu'une interruption
        soit levée.

        Il est également possible d'utiliser :bash:`watch` pour voir les interruptions en direct,
        mais cette commande fonctionne mieux en *SSH*, qui permet d'avoir plusieurs terminaux
        et d'ajuster correctement la taille de ceux-ci :

        .. code-block:: console

            drv-de1:~# watch -n1 "cat /proc/interrupts"

.. admonition:: **Exercice 4**

    Il y a au moins 3 façons pour attendre une interruption au moyen de :file:`/dev/uio0` (voir dans le tutoriel). Lesquelles ?

    Écrivez une version du logiciel user-space de l'exercice 4 pour
    chacune de ces façons et détaillez par écrit leurs
    différences/avantages/inconvénients.

=========================================
Travail à rendre et critères d'évaluation
=========================================

Dans le cadre de ce laboratoire, vous devez rendre les 5 exercices ci-dessus.

.. include:: ../consigne_rendu.rst.inc
