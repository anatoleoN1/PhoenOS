# Essai des mises à jour (0.10.4 → 0.10.5)

But : vérifier sur une vraie machine toute la chaîne des mises à jour. Prévoir ~30 minutes. Tout se fait sous WSL (Windows), `make run`.

## A. Publier la 0.10.4 (une fois)
1. Placez `Phoenos_0.10.4.zip` et `Phoenos_0.10.5.zip` dans Téléchargements, **mais ne publiez d'abord que la 0.10.4** : `bash publier.sh --version 0.10.4 --simulation`, puis, si tout est bon, `bash publier.sh --version 0.10.4`.
2. À la demande, créez la clé de signature (`oui`) et **sauvegardez `~/.phoenos/signing.key` sur une clé USB**.
3. Sur GitHub, la release `0.10.4` doit contenir : `Phoenos_0.10.4.zip`, `phoenos-perf32-0.10.4.iso`, `manifest-perf32.txt`, `manifest-perf32.txt.sig`, `phoenos-perf32-0.10.4.kernel`, `phoenos-perf32-0.10.4.initrd`, `SHA256SUMS`.
   (Si `publier.sh` traite aussi la 0.10.5 : relancez avec `--version 0.10.4` seulement ; voir aussi la partie C.)

## B. Démarrer la 0.10.4 depuis son ISO
1. Dézippez la 0.10.4 ailleurs, ou utilisez l'ISO fabriquée par `publier.sh` (`PhoenOS/iso/phoenos-perf32-0.10.4.iso`, elle contient VOTRE clé publique).
   Une ISO faite avec `make iso` sur un zip brut a une clé vide : `python3 tools/phoenos_sign.py applykey` avant.
2. `make newdisk` (disque de 32 Mio au moins), lancez QEMU avec ce disque (`make run` ou `make serial`).
3. Dans PhoenOS : `maj` (état) → « cle de signature : <empreinte> ». Puis `maj preparer`. Puis `maj verifier` : tant que la 0.10.5 n'est pas publiée, il doit dire « Deja a jour ».

## C. Publier la 0.10.5 puis l'installer
1. `bash publier.sh` (publie la 0.10.5, qui devient « Latest »).
2. Dans PhoenOS (toujours la 0.10.4) : `maj verifier` → « Mise a jour disponible : PhoenOS 0.10.5 ... Signature valide ».
3. `maj installer` → écriture dans l'emplacement A, puis « PhoenOS 0.10.5 est installe ». `maj` montre « emplacement A : PhoenOS 0.10.5 <- actif (a l'essai) ».
4. `reboot` (sous `make run`, QEMU se ferme : relancez-le avec le même disque).
5. Au démarrage, GRUB lance la 0.10.4 du CD, puis la ligne « Mises a jour : lancement de la version 0.10.5 » doit apparaître et le système doit continuer sur la **0.10.5** (message d'accueil « PhoenOS 0.10.5 », `maj` : « version installee sur le disque »).
6. Attendez 30 secondes : `maj` ne doit plus afficher « (a l'essai) » (version confirmée).

## D. Essais de sécurité (facultatifs)
- `maj retour` puis `reboot` : revient à la 0.10.4 du CD.
- Sans attendre 30 s après l'étape C5, faites `reboot` deux fois de suite : après 2 essais non confirmés PhoenOS doit revenir tout seul à la 0.10.4 (« retour arriere effectue »).
- `maj installer` pendant l'essai de 30 s doit être refusé.

## E. Ce qu'il faut m'envoyer
Le texte complet de l'écran au démarrage de l'étape C5 (surtout les lignes `Mises a jour`), la sortie de `maj` avant/après, et `logs` (journal) si quelque chose ne va pas. Pour `make debug`, le fichier du dossier `logs/`.
Si le démarrage de la 0.10.5 plante ou reboucle : redémarrez, tapez `maj retour` si vous avez la main, sinon refaites un disque neuf (`make newdisk`) : le CD reste toujours le point de retour.
