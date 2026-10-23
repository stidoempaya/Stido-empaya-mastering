# STIDO Empaya Mastering (JUCE, VST3 / AU / AAX)

Interface et paramètres entièrement en français.

Chaîne de mastering stéréo :

**Input → EQ (shelf grave 100 Hz, cloche médium réglable, shelf aigu 8 kHz) → Dé-esseur → Compresseur "glue" → Makeup → Width (Mid/Side) → Drive → Limiteur (ceiling) → Output → Normalisation**

Organisation :
1. le **moteur de mastering** traite le son (égaliseur, dé-esseur, compresseur, largeur, poussée, limiteur) ;
2. le **profil** (Apple Music, Spotify, YouTube, Deezer, Tidal, Amazon Music) définit la cible de loudness et le plafond true peak ;
3. la **normalisation** ajuste le niveau final si nécessaire : le bouton « Normaliser » calcule un gain fixe à partir du LUFS intégré réellement mesuré, sans dépasser le plafond true peak du profil ;
4. le **Gain Match** sert uniquement à comparer à l'oreille le son traité et l'original à volume égal : il ne modifie ni les réglages ni les mesures.

Deux vues : **Simple** (comme LANDR : Analyser, styles, intensité, loudness et 6 réglages essentiels) et **Avancée** (tous les réglages). Le choix est sauvegardé avec le projet.

Un indicateur de crête L/R et un bouton Bypass sont inclus, ainsi que :
- un **limiteur true peak avec anticipation** (2 ms, détection sur le signal suréchantillonné ×4) : le Plafond est en dBTP ;
- un **analyseur de spectre** du son en sortie, avec la courbe de l'égaliseur superposée ;
- un bouton **Analyser** : en jouant le refrain (~20 s), le plugin compare le spectre du morceau à une courbe de référence et corrige l'égaliseur en douceur, puis règle la Poussée pour atteindre la cible LUFS de la plateforme choisie (et baisse le Plafond si le true peak dépasse -1 dBTP) ;
- des **styles** (Chaud, Équilibré, Ouvert : égaliseur, dé-esseur, largeur, vitesse du compresseur) et une **Intensité** (Faible, Moyenne, Forte : compression, poussée, limiteur), qui règlent plusieurs boutons en un clic, comme LANDR ; tout reste ensuite réglable à la main ;
- un **dé-esseur** (adoucit les « s » trop aigus : Quantité 0 % = désactivé, Fréquence 3 à 12 kHz) ;
- un bouton **Gain Match** : le master est écouté au même volume que l'original, pour le comparer honnêtement avec Contourner (les mesures restent celles du master) ;
- un **indicateur de loudness** (norme ITU-R BS.1770 / EBU R128) : LUFS intégré, court terme, momentané et true peak, avec la cible de la plateforme choisie (Apple Music -16 LUFS, Spotify -14, YouTube -14, Deezer -15, Tidal -14 : true peak -1 dBTP ; Amazon Music -14 LUFS, -2 dBTP) et un conseil de réglage.

Tous les paramètres sont automatisables et sauvegardés avec le projet.

## Prérequis
- CMake ≥ 3.22
- Un compilateur C++17 : Visual Studio 2022 (Windows), Xcode (macOS), GCC/Clang + paquets JUCE Linux
- Git (JUCE 8.0.4 est téléchargé automatiquement)

## Compilation

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Le plugin est copié automatiquement dans le dossier VST3 (et AU sur macOS) du système.
Une version Standalone est aussi générée pour tester sans DAW.

## Obtenir le fichier du plugin sans compiler chez toi (GitHub Actions)

1. Crée un dépôt GitHub (gratuit) et envoie-y tout le contenu de ce dossier, y compris `.github/`.
2. Ouvre l'onglet **Actions** du dépôt : la compilation démarre toute seule (10 à 20 minutes).
3. Quand elle est terminée, ouvre l'exécution et télécharge **STIDO-Empaya-Mastering-windows-latest** ou **STIDO-Empaya-Mastering-macos-latest** dans la section *Artifacts*.
4. Dans l'archive : `VST3/STIDO Empaya Mastering.vst3` et `Standalone/`.

Installateurs : **STIDO-Empaya-Mastering-Installer-macOS** (`.dmg`) et **STIDO-Empaya-Mastering-Installer-Windows** (`.exe`, créé avec Inno Setup à partir de `installer/windows.iss`).

Installation manuelle : copie le `.vst3` dans `C:\Program Files\Common Files\VST3` (Windows) ou `~/Library/Audio/Plug-Ins/VST3` (macOS).
Compatibilité Mac : binaire universel, de **macOS 10.13 High Sierra** à la dernière version sur les Mac Intel, et à partir de **macOS 11 Big Sur** sur les Mac Apple Silicon (M1, M2, M3, M4…).

Le format AAX n'est pas compilé ici : il exige le SDK privé d'Avid, à utiliser sur ta machine.

## Pro Tools (AAX)

1. Crée un compte développeur gratuit sur developer.avid.com et télécharge le **SDK AAX**.
2. Configure avec le chemin du SDK :

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DAAX_SDK_PATH="C:/chemin/vers/AAX_SDK"
cmake --build build --config Release
```

3. Le plugin `.aaxplugin` est généré (Windows et macOS uniquement, Pro Tools 64 bits).
4. **Signature obligatoire** : la version commerciale de Pro Tools ne charge que les plugins AAX signés avec l'outil `wraptool` (PACE / iLok). Cette signature s'obtient via le programme développeur d'Avid, et un iLok est nécessaire pour l'étape de signature. Sans elle, le plugin n'apparaît pas dans Pro Tools.

## Pistes d'amélioration
- Compresseur multibande (ajouter `juce::dsp::LinkwitzRileyFilter`)
