# DarkMPE

Generatore di lead MPE in stile Gesaffelstein e trasformatore di MIDI in voicing MPE. Formati VST3, AU e Standalone, pensato per Ableton Live 12.

## Build
```bash
cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build -j8
ctest --test-dir build        # test del motore e del processor (verifica che l'output sia MPE)
```
Per copiare i plugin in `~/Library/Audio/Plug-Ins` dopo la build aggiungi `-DDARKMPE_INSTALL=ON` alla configurazione, oppure copia a mano da `build/DarkMPE_artefacts/Release/{VST3,AU}`.

Demo pronte all'uso (ogni stile e ogni voicing dei `.mid` in `Examples/`):
```bash
./build/DarkMPETests_artefacts/Release/DarkMPETests --render Examples "Examples/MPE Output"
```

## Uso in Live 12
1. **Traccia A**: carica DarkMPE, che compare tra gli strumenti (Plug-ins → VST3 → Laherte).
2. **GENERATE**: scegli Style, Key, Scale e Bars e muovi le manopole. Ogni modifica rigenera con lo stesso seed. **NEW** crea un nuovo seed; **MUTATE** varia solo le battute di risposta.
3. **TRANSFORM**: trascina un `.mid` (anche MPE polifonico: le note vengono accoppiate per canale, e pitch bend, CC74, pressure e aftertouch diventano curve per nota; con *Keep Input Expr* l'espressione di chi suona viene conservata) sulla finestra del plugin, oppure usa **LOAD MIDI**, oppure **CAPTURE** (armi, suoni sulla traccia A, premi di nuovo). Poi scegli il voicing: Drop 2, Open Spread, Dark Cluster, Quartal, Power + Oct, Add 9+11 o Unison Stack.
4. **CINEMATIC**: trasforma gli accordi caricati (o, se non carichi niente, una progressione demo i–VI–III–VII nella Key/Scale scelta) in un movimento MPE da colonna sonora:
   - **Motion**:
     - *Morph*: ogni voce è una sola nota lunga che scivola da un accordo all'altro;
     - *Bloom*: ogni accordo si apre a ventaglio da un unisono;
     - *Collapse*: ogni accordo si richiude nell'unisono;
     - *Breathe*: prima si apre, poi si richiude.
   - **Reharmonise**:
     - *Sus Resolve*: la 4ª scende sulla 3ª con il bend;
     - *Chromatic Approach*: l'ultimo tempo è l'accordo successivo mezzo tono sopra, che poi ci scivola dentro;
     - *Mediant Shift*: la seconda metà di ogni accordo si sposta a una mediante cromatica.
   - **Voicing**: *Epic Spread* su 3 ottave, oppure uno qualsiasi degli altri voicing.
   - **Glide / Anticipate / Stagger / Glide Shape**: durata del glide, quanto arriva in anticipo sul cambio d'accordo, sfalsamento tra le voci e forma della curva (lineare, ease, swoop).
   - **Swell**: crescendo di pressure e timbro (CC74) su ogni accordo; la voce più acuta ha anche un vibrato lento.
5. **DRAG MPE ▶ LIVE / EXPORT**: il `.mid` è MPE vero (una nota per canale 2-16, con bend, CC74 e pressure per nota) e funziona in Bitwig, Logic, Cubase e Reaper. **Ableton Live però importa i `.mid` senza MPE**: fonde i canali in un'unica curva di Pitch Bend della clip. Lo stesso vale per l'export di Live, che non scrive l'MPE. Per avere clip MPE in Live usa la porta qui sotto.

## MPE vero in Live 12: porta "DarkMPE Out"
Il plugin pubblica una porta MIDI virtuale (**DarkMPE Out**, toggle *MIDI Port Out*). Live accetta l'MPE nativo solo da una porta di input con MPE Mode, mentre il routing "MIDI From: traccia" tra tracce appiattisce tutto in un pitch bend globale.

1. **Traccia A**: DarkMPE, con input impostato su *No Input*.
2. **Live → Settings → Link, Tempo & MIDI**: sulla riga di Input **DarkMPE Out** attiva **Track** e **MPE**.
3. **Traccia B**: il synth (Serum 2, Drift, Wavetable, Meld…).
   - MIDI From = **DarkMPE Out**, Monitor = **In**.
   - Per i plugin (Serum): tasto destro sul titolo del device → **Enable MPE Mode**; dentro Serum attiva l'MPE con bend range 48.
4. Premi Play in Live (oppure PREVIEW nel plugin): il synth riceve l'MPE per nota. La striscia **MPE OUT** sotto il piano roll mostra canale per canale nota, bend, slide e pressure che escono.
5. **Clip MPE**: arma la traccia B e registra. La clip ha il pitch per nota nel tab *MPE* (Note Expression).

Con più istanze le porte si chiamano "DarkMPE Out 2", "DarkMPE Out 3" e così via.

Per capire come viene letto un file MIDI:
```bash
./build/DarkMPETests_artefacts/Release/DarkMPETests --inspect "file.mid"
```

## Struttura
- `Source/engine/CinematicEngine`: regioni di accordi, reharm, voicing a slot e movimenti Morph/Bloom/Collapse/Breathe.
- `Source/engine/MelodyGenerator`: ritmo euclideo, motivo, pedale, ottave, cromatismi e slide.
- `Source/engine/VoicingEngine`: rilevamento degli accordi, revoicing, voice leading a movimento minimo, strum.
- `Source/engine/ExpressionShaper`: glide, detune, vibrato, curve di timbro e pressure.
- `Source/engine/MpeRenderer`: allocazione dei canali MPE della Lower Zone (2-16), PB ±48.
- `Source/engine/MidiFileIO`: import ed export `.mid` (960 PPQ).
