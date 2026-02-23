.. _flash_sd:

==========================
Flasher la carte SD
==========================

Un cas de problème avec l'image de la carte SD, il est possible de la reflasher.

Pour cela, il faut télécharger l'archive :file:`drv-2026-sdcard.tar.gz` contenant l'image de la carte SD [disponible `ici <http://reds-data.heig-vd.ch/cours/2026_drv/drv-2026-sdcard.tar.gz>`__]

Une fois téléchargé l'archive contenant l'image de la carte SD, vous devez identifier le dispositif associé avec le lecteur de cartes avec la commande **dmesg**:

.. code-block:: console

   HOST:~$ dmesg
   ...
   130088.305550] sd 6:0:0:1: [sde] 31422464 512-byte logical blocks: (16.1 GB/15.0 GiB)
   [130088.306567] sd 6:0:0:1: [sde] Write Protect is off
   [130088.306575] sd 6:0:0:1: [sde] Mode Sense: 2f 00 00 00
   [130088.307716] sd 6:0:0:1: [sde] Write cache: disabled, read cache: enabled, doesn't support DPO or FUA
   [130088.313749]  sde: sde1 sde2 sde3
   [130088.316561] sd 6:0:0:1: [sde] Attached SCSI removable disk

.. figure:: images/skull.png
   :width: 6cm
   :align: center

Dans **mon** cas, il s'agit de :file:`/dev/sde`. FAITES BIEN ATTENTION AU NOM QUE
**VOUS** OBTENEZ ! Si le mauvais périphérique est utilisé, cela peut réécrire sur
votre système !

:boldred:`Dans la commande ci-dessous, il faudra remplacer`
:file:`VOTREDISPOSITIF`
:boldred:`avec le nom du dispositif que vous avez obtenu grâce à dmesg.`

Vous pourrez ainsi préparer la carte avec les commandes :

.. code-block:: console

   HOST:~$ tar xzvf drv-2025-sdcard.tar.gz
   HOST:~$ sudo dd if=drv-2024-sdcard.img of=/dev/VOTREDISPOSITIF bs=4M conv=fsync status=progress

Replacez la carte SD dans la board DE1-SoC une fois la copie terminée !

Vous trouverez davantage d'informations sur la commande :code:`dd` `ici <https://en.wikipedia.org/wiki/Dd_(Unix)>`__.
