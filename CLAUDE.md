# STIDO Empaya Mastering — mémoire du projet

Fichier lu automatiquement par Claude Code au début de chaque session.
Il résume l'historique du projet et les décisions prises, pour reprendre le travail sans tout réexpliquer.

## Le projet
- Plugin de mastering stéréo en C++ / JUCE 8.0.4 (VST3, AU, Standalone ; AAX possible avec le SDK Avid).
- Interface et paramètres **entièrement en français**. Répondre à l'utilisateur en français, avec des explications simples et pas à pas.
- Code : `source/PluginProcessor.*` (traitement audio), `source/PluginEditor.*` (interface).
- Compilation : GitHub Actions (`.github/workflows/build.yml`) sur macOS et Windows à chaque push. L'utilisateur n'a pas d'environnement de compilation local : il récupère l'installateur `.dmg` dans *Actions → Artifacts* (STIDO-Empaya-Mastering-Installer-macOS).
- L'utilisateur fusionne lui-même les pull requests sur GitHub (« Merge pull request »). Travailler sur une branche, ouvrir une PR via l'API REST (`gh api`), et lui donner le lien.

## Historique
### Octobre 2026 — compatibilité Mac (PR #1, fusionnée)
- Version minimale de macOS : 10.13 sur Intel, 11.0 sur Apple Silicon (`CMAKE_OSX_DEPLOYMENT_TARGET`, binaire universel arm64 + x86_64).
- Installateur `.pkg` : `hostArchitectures="x86_64,arm64"` (plus de demande Rosetta) et `allowed-os-versions` 10.13.
- Étape de CI « Check macOS compatibility » : elle échoue si un binaire n'est pas universel ou si sa version minimale de macOS n'est pas la bonne.

### Octobre 2026 — spectre et loudness (PR #2, branche `spectre`)
- Analyseur de spectre de la sortie (FFT 2048, recouvrement 50 %, 20 Hz à 20 kHz), avec la courbe de l'égaliseur superposée.
- Indicateur de loudness ITU-R BS.1770 / EBU R128 : LUFS intégré (portes -70 LUFS / -10 LU), court terme, momentané, true peak (suréchantillonnage ×4).
- Cibles par plateforme (paramètre `target`) : Apple Music -16, Spotify -14, YouTube -14, Deezer -15, Tidal -14, Amazon Music -14 LUFS ; -1 dBTP. Conseils affichés (Poussée / Plafond).
- Algorithme LUFS vérifié hors du plugin avec le signal de référence EBU (sinus 1 kHz à -23 dBFS en stéréo → -23,0 LUFS).
- Fenêtre : 760 × 740.

## Pro Tools (AAX) — en attente de l'utilisateur
- Pro Tools n'accepte que l'AAX : non compilé pour l'instant.
- Étapes côté utilisateur : 1) compte développeur sur developer.avid.com/aax et téléchargement du SDK AAX ; 2) code d'activation Pro Tools Developer via devauth@avid.com, pour tester sans signature ; 3) outils de signature PACE via audiosdk@avid.com (objet « PACE Eden Signing Tools Request »), plus un iLok.
- Le dépôt est **public** : ne jamais y mettre le SDK AAX. Le stocker dans un dépôt privé ou en secret, et l'ajouter à la CI quand l'utilisateur l'aura.
- En attendant, l'utilisateur teste dans FL Studio (démo), GarageBand, Audacity ou l'application autonome.

## Pistes pour la suite
- Compiler l'AAX, puis le signer (quand le SDK et les outils PACE seront disponibles).
- Installateur Windows (comme le `.dmg` macOS).
- Limiteur avec lookahead et true peak (actuellement le Plafond limite les échantillons, pas le true peak).
- Compresseur multibande.
- Signature et notarisation Apple (compte développeur Apple à 99 $/an), pour supprimer l'avertissement de Gatekeeper.
