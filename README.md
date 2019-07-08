# P2PJS

Peer-To-Peer Jobsystem.

Programmierprojekt für das Modul Peer-To-Peer Systeme im SoSe 19.

## Kompilieren:

Unter Linux/macOS:
1. gcc/clang installiert haben. Jede Version die c11 unterstützt, sollte funktionieren.
2. `./build.sh` im Projektordner ausführen.

## Ausführen:

Die ausführbare Datei heißt per-Default `p2pjs`.
Ausführen ohne Parameter startet das Programm mit den Standardoptionen.

Ansonsten stehen folgende Parameter zur Verfügung:

| Option    | Erklärung      |
|---        |---    |
| `-p`      | Port auf dem nach eingehenden Verbindungen gelauscht wird. Standard: 2096 |
| `-f`      | First-Peer. IP-Adresse und Port des ersten Peers mit dem sich verbunden wird. Muss ein String der Form `ip#port` sein. Standard: Leer |
| `-b`      | Fork-To-Background. Startet das Programm als Daemon im Hintergrund. Standard: Aus |

## Benutzte Bibliotheken

* sha-2: SHA-256 Implementierung (Public Domain) von: https://github.com/amosnier/sha-2
* wren: Skriptsprache (MIT Lizenz) von https://github.com/wren-lang/wren
