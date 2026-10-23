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

### Octobre 2026 — plan « comme LANDR » (étapes A à D, une PR par étape)
- Ordre validé par l'utilisateur : A dé-esseur + Gain Match → B styles (Chaud, Équilibré, Ouvert) + Intensité → C bouton Analyser (règles, pas d'IA : spectre vs courbe de référence + LUFS vs cible) → D vues « Simple » / « Avancé ».
- A (branche `deesseur`) : dé-esseur à bande séparée (Linkwitz-Riley, détection haute bande / pleine bande, ratio 4:1) après l'égaliseur ; Gain Match en sortie (K-weighting, moyenne ~3 s, ±24 dB), appliqué après les mesures et figé pendant Contourner. Nouveaux paramètres ajoutés en fin de liste : `deessAmount`, `deessFreq`, `gainMatch`.
- B (branche `styles`, construite sur `deesseur`) : styles et intensités = raccourcis (pas des paramètres automatisables) qui écrivent plusieurs paramètres depuis l'interface (`applyStyle` / `applyIntensity` dans le processeur). Le style touche le timbre (EQ, dé-esseur, largeur, attaque/relâchement), l'intensité le niveau (seuil, ratio, compensation, Poussée, Plafond, relâchement du limiteur) ; ils ne se chevauchent pas. Dernier choix gardé dans `apvts.state` (`style`, `intensity`). Rangée « STYLE / INTENSITÉ » sous le spectre, fenêtre toujours 760 × 780.
- C (branche `analyser`, construite sur `styles`) : bouton « Analyser » dans l'en-tête, machine à états dans l'éditeur (`updateAnalysis`, timer 30 Hz). Phase 1 « écoute » : l'analyseur de spectre reçoit l'entrée (`analyseInput`), puissance moyenne par bin sur 8 s de son non silencieux → `computeAnalysisEq` (bandes 1/3 d'octave vs pente de référence -3,5 dB/oct, corrections = moitié de l'écart, ±3 dB ; médiums : boue 300 Hz ou dureté 3150 Hz si écart > 1,5 dB). Logique vérifiée en Python sur des spectres synthétiques. Correction stockée dans `apvts.state` (`anaValid`, `anaLow`, `anaHigh`, `anaMid`, `anaMidFreq`) ; les styles s'ajoutent par-dessus (décalages graves/aigus). Phase 2 « réglage » : style + intensité (Équilibré / Moyenne par défaut), puis jusqu'à 4 fenêtres de 3,5 s : LUFS court terme vs cible → Poussée (puis Sortie vers le bas), Plafond baissé si true peak > -1 dBTP.
- La liste des plateformes (cible LUFS) ne change pas le son directement : elle sert de cible au bouton Analyser et à la normalisation.

### Octobre 2026 — architecture demandée par l'utilisateur (branche `normalisation`, construite sur `analyser`)
1. Moteur de mastering (EQ, dé-esseur, compresseur, largeur, Poussée, limiteur, Sortie). 2. Profil = cible LUFS **et** plafond true peak (`LoudnessTarget { name, lufs, truePeak }` ; Amazon Music -2 dBTP, les autres -1). 3. Normalisation : paramètre `normGain` (±12 dB) appliqué après Sortie, avant les mesures ; calculé par le bouton « Normaliser » = cible - LUFS intégré mesuré, plafonné par (plafond TP - true peak mesuré) ; gain fixe, pas de variation pendant la lecture. 4. Gain Match : écoute seulement, après les mesures (sa mesure de sortie est prise après la normalisation) ; séparé de la mesure LUFS et de la normalisation.
- Chaîne finale : moteur → normalisation → mesures (= export) → Gain Match → sortie. L'analyse remet `normGain` à 0 et règle le moteur ; la normalisation se fait ensuite sur le morceau entier.
- Bug corrigé : la courbe EQ n'était pas dessinée (`Path::isEmpty()` reste vrai après `startNewSubPath`). Gain Match toujours désactivé à l'ouverture d'un projet + bandeau d'avertissement. Fenêtre : 760 × 800.

### Octobre 2026 — étape D + installateur Windows + limiteur true peak (branche `etape-d`, depuis `main` après la fusion de la PR #6)
- Installateur Windows : `installer/windows.iss` (Inno Setup 6, UTF-8 avec BOM, français) ; VST3 dans `{commoncf64}\VST3`, application dans Program Files. CI : `iscc` (installé par Chocolatey s'il manque), artefact `STIDO-Empaya-Mastering-Installer-Windows`.
- Limiteur : l'ancien `juce::dsp::Limiter` ne respectait pas le Plafond (gain de rattrapage puis écrêtage à 0 dBFS). Remplacé par `source/TruePeakLimiter.h` : détection sur le signal suréchantillonné ×4, anticipation 2 ms (maintien du gain minimal + moyenne glissante de même longueur), relâchement exponentiel, latence déclarée (`setLatencySamples`), Contourner garde le même retard. Logique vérifiée en simulation (sortie max = plafond). L'analyse règle le Plafond = plafond TP du profil. Sortie et Normalisation restent après le limiteur (Normaliser plafonne par la marge TP).
- Vues : `apvts.state` « view » (1 = simple, par défaut). Vue simple 760 × 490 : en-tête, spectre + LOUDNESS, STYLE / INTENSITÉ, 6 boutons (Graves, Médiums, Aigus, Largeur, Poussée, Dé-esseur). Vue avancée 760 × 800 : tous les boutons. Bouton « Vue avancée / Vue simple » au bout de la rangée STYLE / INTENSITÉ.

## Pro Tools (AAX) — en attente de l'utilisateur
- Pro Tools n'accepte que l'AAX : non compilé pour l'instant.
- Étapes côté utilisateur : 1) compte développeur sur developer.avid.com/aax et téléchargement du SDK AAX ; 2) code d'activation Pro Tools Developer via devauth@avid.com, pour tester sans signature ; 3) outils de signature PACE via audiosdk@avid.com (objet « PACE Eden Signing Tools Request »), plus un iLok.
- Le dépôt est **public** : ne jamais y mettre le SDK AAX. Le stocker dans un dépôt privé ou en secret, et l'ajouter à la CI quand l'utilisateur l'aura.
- En attendant, l'utilisateur teste dans FL Studio (démo), GarageBand, Audacity ou l'application autonome.

## Pistes pour la suite
- Compiler l'AAX, puis le signer (quand le SDK et les outils PACE seront disponibles).
- Compresseur multibande.
- Signature et notarisation Apple (compte développeur Apple à 99 $/an), pour supprimer l'avertissement de Gatekeeper.
