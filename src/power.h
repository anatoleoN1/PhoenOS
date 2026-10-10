#ifndef PHOENOS_POWER_H
#define PHOENOS_POWER_H

/* Redémarre la machine (contrôleur clavier, puis triple faute en dernier recours). */
void power_reboot(void) __attribute__((noreturn));

/*
 * Éteint la machine : fonctionne sous QEMU et VirtualBox. Sur un vrai PC (ACPI
 * pas encore géré), affiche un message et arrête le processeur.
 */
void power_shutdown(void) __attribute__((noreturn));

/* Arrête le processeur sans éteindre. */
void power_halt(void) __attribute__((noreturn));

#endif
