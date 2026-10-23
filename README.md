# STIDO Empaya Mastering (JUCE, VST3 / AU / AAX)

Interface et paramètres entièrement en français.

Chaîne de mastering stéréo :

**Input → EQ (shelf grave 100 Hz, cloche médium réglable, shelf aigu 8 kHz) → Compresseur "glue" → Makeup → Width (Mid/Side) → Drive → Limiteur (ceiling) → Output**

Un indicateur de crête L/R et un bouton Bypass sont inclus. Tous les paramètres sont automatisables et sauvegardés avec le projet.

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

Installation : copie le `.vst3` dans `C:\Program Files\Common Files\VST3` (Windows) ou `~/Library/Audio/Plug-Ins/VST3` (macOS).
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
- Mesure LUFS (filtre K-weighting + intégration 400 ms / 3 s)
- Limiteur avec lookahead et détection true-peak (suréchantillonnage via `juce::dsp::Oversampling`)
- Analyseur de spectre (FFT) dans l'éditeur
