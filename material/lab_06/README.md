# Lab 05

## Général

- Auteur: Fabien Léger
- Cours: DRV, HEIG-VD
- Date: 04.05.2026

## Questions

> Un ou plusieurs drivers existent déjà pour ce capteur dans le noyau Linux. Saurez-vous les trouver ? Quels frameworks sont utilisés ?
>
> Notre driver sera différent, mais il peut toujours être utile d'analyser des drivers existants pour comprendre leur fonctionnement.

On peut le chercher en tappant `adxl345` ce qui nous amène au dts de la de1-soc. Ensuite, on peut regarder le 
référencement de la compatibilité ce qui nous amène à `/drivers/iio/accel/adxl345_i2c.c`. On se rend donc compte que les
frameworks utilisés sont iio et accel, probablement, car c'est un accéléromètre.

> D'après ce devicetree, quelle est l'adresse du capteur sur le bus I2C ? Cela correspond-t-il à l'information que vous trouvez dans le datasheet de l'ADXL345 ?

Il est marqué la chose suivante.

```dts
&i2c1 {
    /* ... */
};
```

Cela signifie que le numéro de bus est 1. En cherchant sur la de1-soc, la seule référence que j'ai trouvée est celle-ci.

```file
0xFFC04000 0xFFC040FC HPS I2C0
```

Il semble donc que le bus doit être le bus 0.

> Pourquoi vous a-t-on demandé d'utiliser une routine d'interruption en contexte processus ? Que se passerait-il si
> cette routine était effectuée dans le handler "hard IRQ" et pourquoi ?

En essayant avec une hard IRQ, cela ne marche tout simplement pas. Les fonctions I2C/SMBus peuvent dormir, donc elles
ne doivent pas être appelées dans un handler hard IRQ. Une threaded IRQ veut dire que la gestion se fait alors en
contexte processus et l'endormissement est ainsi possible.

## Implémentation

Exemple à copier pour vérifier fonctionnement du driver.

```shell
cat /sys/devices/platform/soc/ffc04000.i2c/i2c-0/0-0053/tap_axis
echo "x" > /sys/devices/platform/soc/ffc04000.i2c/i2c-0/0-0053/tap_axis
cat /sys/devices/platform/soc/ffc04000.i2c/i2c-0/0-0053/tap_axis
cat /sys/devices/platform/soc/ffc04000.i2c/i2c-0/0-0053/tap_mode
echo "single" > /sys/devices/platform/soc/ffc04000.i2c/i2c-0/0-0053/tap_mode
cat /sys/devices/platform/soc/ffc04000.i2c/i2c-0/0-0053/tap_wait
cat /sys/devices/platform/soc/ffc04000.i2c/i2c-0/0-0053/tap_count
```