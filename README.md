# FICSIT Power Monitor

Un moniteur physique pour **Satisfactory**, basé sur un **ESP32-S3**, un écran OLED et un anneau de 12 LED adressables. Il affiche les données du mod **Ficsit Remote Monitoring (FRM)** : consommation électrique, capacité de production, batteries, bons du broyeur AWESOME et véhicule du joueur.

Le boîtier lit les données par **Wi-Fi / HTTPS** ou directement par **USB / COM**. Son interface Web permet de le configurer sans recompiler le firmware.

## Matériel et câblage

- Carte ESP32-S3 avec câble USB permettant le transfert de données.
- Écran OLED **SH1106, 128 × 64 pixels, I²C**, adresse `0x3C`.
- Anneau de **12 LED NeoPixel compatibles**, format GRB à 800 kHz.
- Alimentation adaptée à la carte et à l’anneau, fils et masse commune.

| Signal | Broche ESP32-S3 |
| --- | --- |
| OLED SDA | GPIO 8 |
| OLED SCL | GPIO 9 |
| Entrée DIN de l’anneau | GPIO 4 |
| Masse OLED / anneau | GND commun |

Alimenter chaque module selon ses spécifications. Les GPIO de l’ESP32-S3 fonctionnent en 3,3 V : vérifier les niveaux des résistances de rappel I²C et, selon l’anneau, prévoir une adaptation du signal de données vers 5 V. Ne pas alimenter les LED depuis un GPIO.

Le pilote est prévu pour un SH1106 : un écran SSD1306 peut nécessiter un autre constructeur U8g2. Les broches, le nombre de LED et le pilote se modifient au début du sketch.

## Dépendances

Installer **Arduino IDE 2.x**, puis le paquet de cartes **esp32 by Espressif Systems** dans le gestionnaire de cartes. Si nécessaire, ajouter cette URL aux URL de gestionnaires de cartes supplémentaires :

```text
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

Dans le gestionnaire de bibliothèques, installer :

| Bibliothèque | Version installée dans l’environnement de développement |
| --- | --- |
| WiFiManager, par tzapu | 2.0.17 |
| ArduinoJson, par Benoit Blanchon | 7.4.3 |
| U8g2, par olikraus | 2.36.19 |
| Adafruit NeoPixel | 1.15.5 |

Le paquet ESP32 utilisé dans cet environnement est **3.3.7**. Ces versions sont des repères de reproduction, pas une garantie de compatibilité avec toutes les cartes. Le code utilise l’API **ArduinoJson 7**.

`WiFi`, `WebServer`, `HTTPClient`, `WiFiClientSecure`, `Preferences`, `Wire`, `ESPmDNS` et FreeRTOS sont fournis avec le paquet ESP32 ; ils ne nécessitent pas d’installation séparée.

Côté jeu, installer et configurer **Ficsit Remote Monitoring** pour une version compatible avec votre installation de Satisfactory. Voir la [documentation FRM](https://docs.ficsit.app/ficsitremotemonitoring/latest/index.html).

## Installation du firmware

1. Placer les sources dans un dossier nommé `FICSIT_Power_Monitor`.
2. Conserver dans ce dossier le sketch `FICSIT_Power_Monitor.ino` et tous ses fichiers `.h` : `ColorSettings.h`, `ConnectionPulse.h`, `CouponFlashEvents.h`, `DisplaySchedule.h`, `SerialJSONFrame.h` et `USBMonitor.h`.
3. Ouvrir `FICSIT_Power_Monitor.ino` dans Arduino IDE.
4. Sélectionner la carte correspondant au matériel. La configuration de développement utilise **ESP32S3 Dev Module**.
5. Choisir **Partition Scheme > Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)**, ou une partition avec un espace programme supérieur adapté à la mémoire flash de la carte. Le firmware dépasse la partition programme par défaut de 1,25 Mio.
6. Pour le connecteur USB natif du S3 : activer **USB CDC On Boot > Enabled** ; le mode **Hardware CDC and JTAG** est pris en charge. Pour un pont USB-UART vers UART0 : laisser **USB CDC On Boot > Disabled**.
7. Sélectionner le bon port, vérifier/compiler, puis téléverser.

Les scripts Python, Node.js, les fichiers PHP, les dossiers de compilation et les sauvegardes `.before-*` ne sont pas nécessaires au téléversement.

## Première connexion et configuration

1. Au premier démarrage, rejoindre le réseau **FICSIT-POWER-SETUP**, mot de passe initial **12345678**.
2. Ouvrir le portail captif WiFiManager ; s’il ne s’ouvre pas automatiquement, essayer `http://192.168.4.1`.
3. Choisir le Wi-Fi local 2,4 GHz et renseigner son mot de passe.
4. Une fois connecté, ouvrir **http://monitor.local** depuis le même réseau. Si mDNS ne fonctionne pas, utiliser l’adresse IP attribuée par le routeur.
5. S’authentifier avec le mot de passe administrateur initial **1234**, puis le remplacer dans les paramètres par un mot de passe d’au moins **12 caractères**.
6. Dans **Général > Source des données**, choisir le mode réseau ou USB, puis enregistrer.

Les réglages sont conservés dans la mémoire non volatile de l’ESP32 : source, URL, circuits, noms, pages, couleurs et luminosités. Le mode DEBUG est désactivé au redémarrage.

Le mot de passe du point d’accès de configuration se change via `AP_PASSWORD` dans le sketch. Le portail Web utilise HTTP : le réserver à un réseau local de confiance.

## Mode USB / COM : connexion directe à FRM

Ce mode permet de se passer du serveur d’export PHP. Le Wi-Fi reste nécessaire pour accéder au portail de configuration.

1. Brancher l’ESP32 à la machine qui exécute FRM.
2. Dans FRM, sélectionner son port COM et régler **115200 bauds, 8 bits, aucune parité, 1 bit d’arrêt**.
3. Désactiver **Auto_Serial** : le boîtier demande lui-même les données. `COM_Autostart` peut rester activé ; sinon, démarrer le pilote avec `/frm serial start` dans le chat du jeu.
4. Fermer le moniteur série Arduino et les autres logiciels qui utilisent ce port.
5. Choisir **USB / COM FRM** dans le portail du boîtier, puis enregistrer.

Un serveur dédié distant n’a pas directement accès au port COM du PC client. Le branchement doit correspondre à la machine sur laquelle FRM utilise la liaison série.

Le boîtier demande `getPower`, puis les endpoints nécessaires aux options activées : `getResourceSink`, `getCloudInv`, `getStorageInv`, `getPlayer`, `getCrateInv` et `getVehicles`.

Voir [USB_COM.md](USB_COM.md) pour les détails et la [documentation du protocole série FRM](https://docs.ficsit.app/ficsitremotemonitoring/latest/serial.html).

## Mode Wi-Fi / HTTPS : export JSON

Ce mode nécessite un serveur qui transforme les données FRM en un export JSON compatible. **L’API FRM brute n’est pas directement interchangeable avec cet export.**

1. Installer le service d’export côté serveur et lui donner accès à FRM.
2. Vérifier que son URL HTTPS retourne un objet JSON comportant `version: 1`, `online: true`, un `updated_at` récent et un tableau `circuits`.
3. Dans le portail, sélectionner **Réseau / HTTPS**, renseigner l’URL de votre export et enregistrer. L’URL préconfigurée dans le sketch est un exemple lié à l’installation de l’auteur ; la remplacer pour votre propre installation.
4. Pour les bons et véhicules, le serveur doit aussi gérer `?monitor=1` et, pour le conducteur, le paramètre `player`.

Les circuits utilisent notamment `id`, `consumption_mw`, `production_mw`, `capacity_mw`, `max_consumption_mw`, `battery_capacity_mwh`, `battery_percent`, `battery_input_mw`, `battery_output_mw`, `battery_differential_mw`, `battery_time_empty` et `fuse_triggered`.

**Limite des fichiers serveur présents :** `monitor-export.php` est un module d’intégration ; `export.php.reference` est une référence. Ils ne constituent pas un serveur autonome. Ils dépendent notamment de la configuration et du module `includes/power-export.php` du projet serveur, non fournis dans ce dossier. Pour une installation sans ce serveur, utiliser le mode USB.

La connexion HTTPS vérifie le certificat avec `ROOT_CA`, actuellement ISRG Root X2. Adapter cette autorité si votre hébergement utilise une autre chaîne de confiance. L’heure est synchronisée par NTP avant la collecte HTTPS.

## Fonctionnement et affichage

### Énergie et circuits

Le moniteur peut afficher un circuit, une sélection de circuits ou la somme de tous les groupes électriques fournis par FRM. Le mode global inclut tous les identifiants sans exclusion particulière. Le portail permet de personnaliser les noms.

L’écran affiche la consommation, la capacité électrique et les batteries. Quand celles-ci se déchargent, il affiche leur état et leur autonomie. Pour plusieurs groupes de batteries, le pourcentage est pondéré par la capacité ; l’autonomie globale est estimée à partir de l’énergie stockée et de la décharge totale.

### Bons et véhicule

- La page des bons affiche le solde disponible et la progression vers le prochain bon. Le total MAP n’est affiché comme connu que lorsque toutes les sources d’inventaire nécessaires sont valides.
- Les pages énergie et bons peuvent alterner avec des durées configurables de 1 à 600 secondes.
- L’affichage automatique du véhicule prend la priorité lorsqu’un véhicule correspond au pseudo configuré. Le pseudo doit correspondre exactement ; plusieurs correspondances désactivent cette détection.
- La jauge véhicule représente la quantité de carburant rapportée à 100 unités. Elle ne calcule pas une autonomie de conduite.

### Anneau lumineux

| État | Indication |
| --- | --- |
| Connexion Wi-Fi en cours | Pulsation orange |
| Données indisponibles | Une LED orange |
| Énergie en fonctionnement normal | Jauge consommation / capacité, du vert au rouge |
| Décharge des batteries, charge supérieure à 20 % | Jauge bleue |
| Décharge, charge entre 10 % et 20 % inclus | Jauge orange |
| Décharge, charge à 10 % ou moins | Rouge clignotant |
| Déclenchement du disjoncteur | Animation de flash blanc |
| Disjoncteur déclenché sans mode batterie | Rouge clignotant |
| Page bons | Progression avec couleur configurable et flash optionnel au gain de bons |
| Page véhicule | Jauge bleue du carburant |

Les paramètres permettent aussi de désactiver l’écran ou l’anneau et de régler le contraste OLED, la luminosité et les flashs.

### Acquisition et simulation

En réseau, les collectes sont déclenchées à un intervalle d’environ 3 secondes lorsque la boucle est disponible ; les délais HTTP peuvent allonger ce rythme. En USB, les cycles sont espacés d’au moins 3 secondes, avec une durée dépendant du nombre et du volume des réponses FRM.

Les mesures électriques USB expirent après 30 secondes sans renouvellement. En HTTPS, l’horodatage de l’export est contrôlé avec un âge maximal de 30 secondes. Une réponse invalide provoque un état d’erreur. Le retour entre réseau et USB est manuel.

Le mode **DEBUG** du portail simule les mesures, les batteries, les bons, le véhicule et les événements pour vérifier les affichages sans dépendre des données du jeu.

## Dépannage

| Problème | Vérifications |
| --- | --- |
| Le programme est trop gros | Sélectionner la partition Minimal SPIFFS ou une partition programme plus grande. |
| Écran noir | Vérifier le modèle SH1106, l’adresse `0x3C`, SDA 8 / SCL 9, l’alimentation et l’activation de l’écran dans le portail. |
| Anneau éteint | Vérifier GPIO 4 vers DIN, le sens de l’anneau, l’alimentation, la masse commune et son activation. |
| `monitor.local` ne répond pas | Utiliser l’IP du boîtier et vérifier que les appareils sont sur le même réseau sans isolation des clients. |
| Attente NTP ou erreur HTTPS | Vérifier l’accès Internet, l’heure, l’URL et la chaîne du certificat. |
| JSON invalide ou serveur hors ligne | Vérifier le format d’export et sa fraîcheur ; ne pas fournir directement l’API FRM brute au mode HTTPS. |
| Pas de données USB | Vérifier le câble, le port, les options CDC, FRM, Auto_Serial désactivé et la fermeture du moniteur série. |
| Bons MAP ou carburant inconnus | Vérifier les endpoints FRM nécessaires et le pseudo du conducteur ; une source manquante n’est pas assimilée à zéro. |

## Organisation des sources et vérification

- `FICSIT_Power_Monitor.ino` : configuration Web, acquisition HTTPS, agrégation, écran et animations.
- `USBMonitor.h`, `SerialJSONFrame.h` : acquisition FRM par série et lecture des trames JSON.
- `DisplaySchedule.h` : alternance des pages.
- `ConnectionPulse.h`, `CouponFlashEvents.h`, `ColorSettings.h` : animations et couleurs.
- `USB_COM.md` : guide de la liaison série.
- `tests/`, `run_*_tests.cmd` et `test_*.py` : vérifications de comportements ciblés.
- `MODIFICATIONS.md` : historique technique des modifications.

Les scripts de tests Windows contiennent des chemins locaux à adapter. Par exemple, `run_usb_tests.cmd` utilise Visual Studio 2022 Community, le compilateur C++ MSVC et les en-têtes ArduinoJson. Ils ne sont pas nécessaires pour installer le moniteur. Les tests logiciels ne remplacent pas un essai sur la carte avec FRM.

## Licence

L’objectif du projet est de permettre l’utilisation, l’étude, la modification et le partage à des fins non commerciales.

La licence recommandée pour ces sources est [PolyForm Noncommercial 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0). Elle autorise ces usages dans les conditions prévues par son texte, notamment la transmission de la licence ou de son URL lors du partage. Elle prévoit aussi des autorisations spécifiques pour certaines organisations, détaillées dans son texte.

Cette licence reste à formaliser dans un fichier `LICENSE` ; cette recommandation ne constitue pas à elle seule une mise sous licence. MIT et GPL autorisent l’usage commercial et ne correspondent donc pas à l’objectif retenu.

La licence choisie pour les sources du projet ne remplace pas celles des bibliothèques et composants tiers. Le projet est un outil communautaire indépendant pour Satisfactory.

