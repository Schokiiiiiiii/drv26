.. _laboratoire5:

##########################################################
Laboratoire 5 --- Développement de drivers kernel-space II
##########################################################

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

* Savoir gérer les threads et timers en kernel-space
* Savoir utiliser les queues en kernel-space
* Savoir utiliser les mécanismes pour la configuration depuis le user-space (*command line parameters*, *sysfs*, ...)
* Connaître les mécanismes principaux de synchronisation

===================
Matériel nécessaire
===================

Ce laboratoire utilise le même environnement que le laboratoire précédent,
qui peut donc être réutilisé.

============
Introduction
============

----------------------
Thread noyau (kthread)
----------------------

Il est possible pour un module de démarrer des taches d'arrière-plan, d'effectuer un polling
sur un périphérique à intervalle régulier, ou encore de déléguer le traitement d'une interruption
à un thread afin de ne pas rester dans une routine d'interruption trop longtemps.
Nous avons vu plusieurs mécanismes en cours:

* La "threaded IRQ", qui est spécifique à la gestion des interruptions
* Le thread en espace noyau (kthread)
* La workqueue (qui utilise derrière un ou plusieurs kthreads)

Le kthread est le mécanisme le plus générique.
La macro :c:`kthread_run()` permet de créer un kthread et de la démarrer automatiquement.
En *arrière-plan*, cette macro fait appel à :c:`kthread_create()`, qui crée le kthread sans le démarrer et :c:`wake_up_process()` qui le démarre.
Ces deux fonctions peuvent également être utilisées séparément en fonction des besoins.

Un kthread peut ensuite se terminer de deux manières différentes :

* de manière *spontanée*, en faisant un simple :c:`return`
* lorsque :c:`kthread_stop()` a été appelé depuis un autre thread.

:c:`kthread_stop()` ne tue pas le thread directement,
mais active un flag partagé (comme dans la librairie pco-synchro ;-) indiquant que le thread
doit s'arrêter et bloque en attendant que le thread se termine.
Ce flag peut être testé depuis le kthread au moyen de la fonction :c:`kthread_should_stop()`.
Le kthread doit contrôler périodiquement cette valeur s'il s'attend à être stoppé.
La valeur de retour de :c:`kthread_stop()` correspond à la valeur de retour de la fonction du kthread.

Un thread peut être mis en attente bloquante grâce à la fonction :c:`wait_event()` (et ses nombreuses variantes).
Cette fonction prend comme
paramètre une queue sur laquelle le thread attend et une condition qui doit être respectée pour que
ce thread se réveille.
Le thread peut ensuite être réveillé via un appel à :c:`wake_up()` (et autres variantes), prenant comme paramètre
la queue de threads à réveiller.
Vous pouvez trouver une description de ces fonctions
`ici <https://www.kernel.org/doc/html/latest/driver-api/basics.html#wait-queues-and-wake-events>`__.
Sur le même principe, il est également possible d'utiliser :c:`wait_for_completion()` et :c:`complete()`
dont la documentation est disponible `ici <https://www.kernel.org/doc/html/latest/scheduler/completion.html>`__.

Un exemple d'utilisation de kthread est disponible dans le dossier :file:`example/` dans les sources du labo.

.. warning::

    Attention, ce module écrit régulièrement dans les logs du kernel !
    Ne le laissez pas inséré trop longtemps, au risque de petit à petit remplir votre espace disque !

.. hint::

   Vous ferez peut-être connaissance avec *khungtaskd* durant le laboratoire si vous
   utilisez des attente non-interuptible (uninterruptible wait, affichés "D" dans la sortie de la commande :console:`ps`).

   En effet, ce mécanisme
   est généralement activé par défaut (au moyen de la configuration CONFIG_DETECT_HUNG_TASK)
   et va périodiquement vérifier les tâches en attente non-interuptible et avertir si certaines
   sont "coincées" dans cet état en affichant un message "task xyz:PID blocked for more than yyy seconds"
   avec une *backtrace* de cette tâche.

   Une solution est de recourir à une attente interruptible si votre tâche doit effectivement
   bloquer pendant une longue période de temps.
   Un autre moyen est de faire comme suggéré dans le message
   d'erreur est de désactiver *khungtaskd* en écrivant 0 dans
   :console:`/proc/sys/kernel/hung_task_timeout_secs`, mais ce message a probablement une bonne raison d'être...

--------------
Timer (ktimer)
--------------

Un module peut également faire appel aux timers s'il désire effectuer une tâche cyclique ou compter
précisément un intervalle de temps.
L'interface des timers a beaucoup changé, et il est assez difficile de trouver des exemples qui fonctionnent
(la plupart ne compilent même pas !). Vous pouvez trouver une description de la nouvelle interface
`ici <https://lwn.net/Articles/735887/>`__.

Les principales fonctions sont :c:`timer_setup()` et :c:`mod_timer()`, dont vous trouverez la documentation
dans les `souces du noyau <https://elixir.bootlin.com/linux/v6.1.131/source>`__.

Un exemple d'utilisation de ktimer est disponible dans le dossier :file:`example/` dans les sources du
laboratoire.

.. warning::

    Attention, ce module écrit régulièrement dans les logs du kernel !
    Ne le laissez pas inséré trop longtemps, au risque de petit à petit remplir votre espace disque !

.. hint::

   Les ktimer s'exécute en tant que *softirq*, soit un contexte d'interruption où il est interdit de bloquer,
   tout comme une interruption matériel.
   Il faut donc faire très attention aux fonctions que vous appelez, et notamment aux primitives
   de synchronisation que vous utiliserez dans le dernier exercice.

-------------
Queue (kfifo)
-------------

Un type de structure de données très souvent utilisé lorsque l'on intéragit avec un
vrai périphérique est la queue, aussi appelée **kfifo**.
Le noyau offre une interface qui vous permet facilement d'insérer et de retirer
des éléments.
L'interface est détaillée `ici <https://www.kernel.org/doc/html/latest/core-api/kernel-api.html#fifo-buffer>`__, et
des exemples d'utilisation sont disponibles dans le sous-répertoire :file:`samples` des sources
du noyau.

.. hint::

   Une kfifo offre des garanties limitées en ce qui concerne les accès concurrents.
   Aucun verrouillage n'est nécessaire tant que vous avez un seul lecteur et un seul rédacteur.
   Mais un verrouillage devient nécessaire dès que vous avez plus que un lecteur et/ou rédacteur.
   Voir le commentaire dans le header
   `kfifo.h <https://elixir.bootlin.com/linux/v6.1.131/source/include/linux/kfifo.h#L30>`__.

=========
Exercices
=========

.. admonition:: **Exercice 1 : kfifo et kthread**

    Implémentez un module du noyau qui permet la gestion d'un chenillard (*chaser*) sur les 10 LEDs de la carte DE1-SoC, afin de réaliser les effets lumineux du prochain Baleinev !
    Le but sera d'allumer séquentiellement les LEDs à intervalle régulier, soit de façon montante (LED0 -> LED1, etc.), soit de façon descendante (LED9 -> LED8, etc.).
    Le type de séquence (montante ou descendante) sera contrôlé par l'utilisateur.

    Nous ajouterons le contrôle de l'intervalle, ainsi que la gestion des accès concurrents, dans les prochains exercices.
    Choisissez pour l'instant une valeur par défaut pour l'intervalle qui soit raisonnable, par exemple 1 seconde, afin de pouvoir facilement débugger votre module.
    Libre à vous de rendre cette valeur par défaut configurable lorsque le module est inséré (au moyen d'un paramètre), mais cela n'est pas obligatoire.

    Le driver doit exposer le *device node* :c:`/dev/chaser` qui permet à l'utilisateur de démarrer une nouvelle séquence :

    * :console:`echo "up" > /dev/chaser` démarre une séquence montante (LED0 -> LED9)
    * :console:`echo "down" > /dev/chaser` démarre une séquence descendante (LED9 -> LED0)
    * Tout autre écriture devra être ignorée et un message d'erreur sera imprimé dans les logs du noyau

    Il est possible à l'utilisateur de démarrer plusieurs séquences sans attendre la fin de la séquence en cours.
    Elles doivent être mémorisées (au moyen d'une **kfifo**) afin d'être jouées à la suite, jusqu'à un maximum de 16 séquences en mémoire.
    Par exemple, la séquence suivante :

    .. code-block:: console

       $ echo "up" > /dev/chaser
       $ echo "down" > /dev/chaser
       $ echo "up" > /dev/chaser
       $ echo "down" > /dev/chaser

    devrait permettre de réaliser un effet proche de "K2000" dans le cas où une soirée "Nostalgie 80's" était organisée
    (`pour les plus jeunes d'entre vous <https://www.youtube.com/watch?v=oNyXYPhnUIs>`__ qui ne sont pas nés dans les 1980).

    La lecture du *device node* en fait rien.

    Pour cet exercice, le contrôle des LEDs se fera dans un **kthread** qui est démarré quand le module est inséré dans le noyau.
    Ce kthread sera évidemment proprement terminé quand le module est enlevé.
    Ce kthread devra donc attendre que l'utilisateur écrive une séquence, puis animer les LEDs selon les instructions.
    L'intervalle entre chaque animation sera effectuée au moyen d'un :c:`msleep()` pour l'instant.

    Une fois que la séquence est terminée, les LEDs sont éteintes et le kthread attendra la séquence suivante
    (ou commencera directement à la jouer si une séquence est déjà dans la file d'attente).

    .. hint::

        Ce module sera complété dans les prochains exercices, mais un seul fichier est demandé à la fin.
        Libre à vous de regarder les prochains exercices et de tout implémenter directement ou d'y aller
        exercice par exercice.

.. admonition:: **Exercice 2 : ktimer**

    Le comité de Baleinev se plaint que votre chenillard a trop de *jitter* car le :c:`msleep()` n'est pas assez précis quand le système est trop surchargé.

    Reprenez le code de l'exercice précédent et réalisez l'animation des LEDs au moyen d'un **ktimer**.
    Gardez le **kthread** pour la gestion de la file d'attente, et démarrer le timer lorsqu'une nouvelle séquence doit être jouée.
    Le timer est ensuite responsable de l'animation de la séquence à intervalle régulier et s'arrête lorsque la séquence est finie.

    Le kthread doit attendre la fin de la séquence avant de s'occuper de la séquence suivante (ou d'attendre que l'utilisateur soumette une nouvelle requête).

=====
Sysfs
=====

*Once upon a time...* une interface permettait aux utilisateurs d'interagir
avec le noyau Linux.
Le nom de cette interface était **procfs**, et consistait en un filesystem virtuel avec lequel
le noyau pouvait montrer au user-space des informations sur son fonctionnement et il pouvait
récupérer des paramètres de configuration.
Malheureusement, avec le temps ce filesystem est devenu de plus en plus encombré par des
informations non pertinentes, mais la stabilité de l'ABI avec le user-space
empêchait de faire le ménage.
Ainsi, il a été décidé de repartir de zéro, mais d'une façon plus structurée, avec **sysfs**,
tout en gardant procfs pour le coeur du noyau.
En tant que développeur driver, vous êtes donc censés utiliser sysfs !

Le répertoire du dispositif reds-adder v1.1 (voir :ref:`tutoriel2`) dans :file:`/sys` existe déjà
(grâce à l'appel à
:c:`misc_register()`). On peut le voir dans :file:`/sys/class/misc/` (car il
s'agit d'un miscellaneous device, c.-à-d., il appartient au miscellaneous
framework). En regardant les liens symboliques dans ce répertoire, on voit qu'il
est aussi enregistré en tant que platform device (et donc il a son propre
répertoire :file:`/sys/devices/platform/ff205000.reds-adder`).
Si l'on veut exposer des informations (soit pour nous aider dans le
debugging, soit pour nous permettre de configurer notre dispositif), on peut
créer des fichiers. Ces fichiers peuvent être de trois types :

* en lecture seule (:c:`DEVICE_ATTR_RO`)
* en écriture seule (:c:`DEVICE_ATTR_WO`)
* en lecture/écriture (:c:`DEVICE_ATTR_RW`)

Le noyau nous offre des macros pour nous aider dans la création de ces fichiers.
Par exemple, :c:`DEVICE_ATTR_RW(<name>)` permet d'instancier une structure :c:`device_attribute`
et de l'associer aux deux fonctions :c:`<name>_store()` (écriture) et :c:`<name>_show()` (lecture).
La macro :c:`DEVICE_ATTR()` permet d'instancier la structure de manière plus générique,
en donnant explicitement les permissions d'accès aux fichiers et les fonctions :c:`store` et :c:`show`.
D'autres macros sont également disponibles, `voir la documentation <https://www.kernel.org/doc/html/latest/driver-api/infrastructure.html#c.DEVICE_ATTR>`__

On n'aura ensuite qu'à appeler :c:`device_create_file()` en lui passant notre
dispositif et un pointeur à la structure :c:`dev_attr_<name>` créée par l'une des macros
ci-dessus, et on obtiendra notre fichier dans sysfs.
Cette procédure est présentée dans le driver reds-adder v2.1 (voir :ref:`tutoriel3`).

Dans la fonction :c:`..._store()`, on est censé lire une valeur venant l'utilisateur,
par exemple en utilisant la fonction :c:`kstrtoint()`
(voir `ici <https://www.kernel.org/doc/htmldocs/kernel-api/API-kstrtoint.html>`__)
ou avec :c:`sscanf()`.
Dans la fonction :c:`..._show()`, on retourne à l'utilisateur une valeur,
les fonctions :c:`sysfs_emit()` et :c:`sysfs_emit_at()`
(même fonctionnement que :c:`scnprintf()` voir `ici <https://www.kernel.org/doc/html/latest/filesystems/api-summary.html#c.sysfs_emit>`__)

Voici un exemple d'utilisation :

.. code-block:: c

		static ssize_t my_nice_name_store(struct device *dev,
						  struct device_attribute *attr,
						  const char *buf,
						  size_t count)
		{
			int rc;
			struct priv *priv = dev_get_drvdata(dev);

			rc = kstrtoint(buf, 0, &priv->my_internal_variable);
			if (rc != 0) {
				rc = -EINVAL;
			} else {
				rc = count;
			}

			return rc;
		}

		static ssize_t my_nice_name_show(struct device *dev,
						 struct device_attribute *attr,
						 char *buf)
		{
			int rc;
			struct priv *priv = dev_get_drvdata(dev);

			rc = sysfs_emit(buf, "%d\n", priv->my_internal_variable);

			return rc;
		}

		static DEVICE_ATTR_RW(my_nice_name);

		static int my_driver_probe(struct platform_device *pdev)
		{
			/* ... */
			rc = device_create_file(&pdev->dev, &dev_attr_my_nice_name);
			/* ... */
		}

		static int my_driver_remove(struct platform_device *pdev)
		{
			/* ... */
			device_remove_file(&pdev->dev, &dev_attr_my_nice_name);
			/* ... */
		}

Bien sûr, dans la fonction :c:`..._store()`, on pourrait aussi modifier le
comportement du dispositif, en ajoutant une :c:`iowrite32()` sur le dispositif par exemple.

L'entrée **sysfs** créée par cet exemple est disponible dans :file:`/sys/devices/platform/ff200000.drv2026/my_nice_name`
en utilisant le même platform device que les labos.

.. warning::

    sysfs a deux limitations :

    * on ne peut pas utiliser plus qu'une page de mémoire (normalement 4096 bytes)
    * idéalement un fichier sysfs = une valeur (plus de détail sur cette règle `dans cet article <https://lwn.net/Articles/378884/>`__)

    Si vous voulez sortir plus d'une valeur, il y a d'autres mécanismes
    prévus pour cela (p. ex., *debugfs*), mais qui vont au-delà des
    objectifs de ce cours.

.. admonition:: **Exercice 3 : sysfs**

    Ajoutez, à l'aide de :c:`sysfs`, les fonctionnalités suivantes dans le module de l'exercice précédent :

    * Récupérer et modifier l'intervalle du chenillard
    * Récupérer le numéro de la LED actuellement allumée (ou -1 si aucune séquence n'est en cours)
    * Récupérer le nombre de séquences jouées (i.e. terminées) depuis l'insertion du module dans le noyau
    * Récupérer le nombre de séquences encore dans la kfifo
    * Récupérer la liste des séquences restantes dans la kfifo, une séquence par ligne
      (ce qui enfreint légèrement une des deux limitations énoncées ci-dessus). Par exemple :

      .. code-block:: console

           $ cat /sys/devices/platform/soc/ff200000.drv6/sequence
           up
           down
           down
           up

===============
Synchronisation
===============

Le noyau Linux est conçu pour gérer plusieurs tâches (threads),
potentiellement sur plusieurs processeurs simultanément.
De plus il doit réagir aux événements matériels (interruptions) qui peuvent surgir à n'importe quel moment.
Cela le rend particulièrement exposé aux problèmes de concurrence.

Pour cette raison, il dispose d'un riche ensemble de primitives de synchronisation,
dont vous avez déjà vu certaines en cours :

* mutex
* spinlock
* variable atomique
* RCU
* et beaucoup d'autres...

Plusieurs situations peuvent créer des situations d'accès concurrents à des ressources partagées:

* Des threads user-space qui font appellent aux fonctionnalités exposées par le driver,
  par exemple au travers d'un *device node* ou d'un fichier dans procfs / sysfs / debugfs
* Des threads kernel-space, tels que des kthreads instanciés par le driver lui-même ou tout autre
  mécanisme de traitement déféré
* Des événements qui s'exécute en contexte d'interruption, tels que des *hardirq*, *softirq* ou *tasklets*.

La gestion des accès concurrents dans notre driver passe donc par plusieurs étapes:

* Il faut d'abord reconnaître les différentes situations d'accès concurrent dans notre driver,
  et donc les différentes parties de notre code qui sont exposées à un problème de concurrence
* Il est ensuite possible d'identifier les ressources critiques, soit les ressources partagées
  entre plusieurs tâches concurrentes
* Finalement on peut déterminer le meilleur mécanisme de protection pour chaque ressource critique,
  notamment en connaissant le contexte dans lequel se fait cet accès. Le plus important est
  la distinction entre un contexte *process* et un contexte d'interruption, ce qui limitera
  l'utilisation de certains mécanismes si une attente bloquante est impossible.

Vous pourrez en apprendre plus sur ce sujet dans
le chapitre 5 de `Linux Device Drivers, 3rd edition <https://lwn.net/Kernel/LDD3>`__.

.. admonition:: **Exercice 4: synchronisation**

    Ajouter la gestion des accès concurrents aux différentes variables de votre module.
    Comme contrainte, vous devez avoir au minimum :

    * Une variable atomique
    * Une variable protégée par un spinlock
    * Une variable protégée par un mutex

=========================================
Travail à rendre et critères d'évaluation
=========================================

.. include:: ../consigne_rendu.rst.inc
