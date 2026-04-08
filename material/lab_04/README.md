# Lab04

## Général

- Auteur: Fabien Léger
- Cours: DRV, HEIG-VD
- Date: 08.04.2026

## Implémentation

L'implémentation a réussi après plusieurs essais. Cet endroit sert avant tout de rappel pour des problèmes qui sont
arrivés lors de celle-ci.

Modifier dans le device tree `arch/arm/boot/dts/socfpga_cyclone5_sockit.dts` le numéro d'IRQ si nécessaire. Ne pas
oublier de recompiler le kernel et le copier à l'endroit où la DE1-SoC va le chercher pour le charger en mémoire.

```shell
drv2026 {
    compatible = "drv2026";
    reg = <0xff200000 0x1000>;
    interrupts = <GIC_SPI 41 IRQ_TYPE_EDGE_RISING>;
    interrupt-parent = <&intc>;
};
```

Vu qu'on a ici une GIC_SPI, cela signifie que l'interruption est `shared` et qu'il faut donc réduire de 32 par rapport
à la vraie interruption, car cela sera rajouté par la suite. Donc pour répondre à l'IRQ des keys qui sont sur le numéro
73, il faut mettre le numéro 41.

