# DarkMPE

Generatore MPE in stile Gesaffelstein: lead, armonie cinematiche, tracce complete a layer e trasformazione di MIDI in voicing MPE. Formati VST3, AU e Standalone, pensato per Ableton Live 12.

## Build
```bash
cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build -j8
ctest --test-dir build        # test del motore e del processor (MPE valido, zero allocazioni sull'audio thread)
```
Per copiare i plugin in `~/Library/Audio/Plug-Ins` dopo la build aggiungi `-DDARKMPE_INSTALL=ON` alla configurazione, oppure copia a mano da `build/DarkMPE_artefacts/Release/{VST3,AU}`.

Demo pronte all'uso: ogni stile di lead (MPE e Mono), le Phrase Form (lead e armonia), 10 vetrine CINEMATIC, un KIT completo (un file per layer), e ogni voicing e versione cinematica dei `.mid` in `Examples/`:
```bash
./build/DarkMPETests_artefacts/Release/DarkMPETests --render Examples "Examples/MPE Output"
```

## Le modalità
Ogni modifica rigenera con lo stesso seed.
- **NEW** crea un nuovo seed.
- **MUTATE** varia solo le battute di risposta.
- **◀ ▶** tornano ai seed precedenti (ultimi 32, salvati nel progetto).
- **★** segna un seed tra i preferiti.
- Cliccando **#seed** si apre il menu dei preferiti, oppure puoi scrivere un seed.

### PHRASE FORM (in ogni modalità)
Il selettore **PHRASE FORM** in alto decide la struttura della frase. *Classic* è la generazione di sempre; le altre forme sono strutture di call and response prese dal songwriting e dalla teoria della frase:

| Form | Struttura |
|---|---|
| Call & Response | A B: domanda e risposta aperta |
| A B A C | domanda, risposta aperta, di nuovo la domanda, risposta che chiude |
| A A B A | esposizione, ripetizione, contrasto, ritorno |
| A A A B | tre volte l'idea, poi la svolta |
| Period A B A B' | antecedente e conseguente: la seconda risposta chiude sulla tonica |
| Sentence A A' F C | idea, idea ripresa una terza sopra, frammentazione, cadenza |
| Sequence A A+ A++ C | l'idea sale di un grado alla volta, poi chiude |

Come funziona:
- La stessa lettera suona uguale, nota per nota se l'accordo è lo stesso. **MUTATE** cambia le risposte e non tocca mai A.
- **B** tiene il ritmo e l'inizio di A, specchia il profilo e finisce aperta, sulla quinta.
- **C** riprende l'inizio di A, poi scende per grado alla fondamentale e la tiene.
- **F** ripete la testa di A, un grado più su.

Dove si applica:
- **GENERATE**: il lead.
- **KIT**: il lead, l'arpeggio (le risposte girano al contrario, la chiusura scende a casa) e il basso (ottava sulla risposta, fill sulla cadenza).
- **CINEMATIC**: la progressione viene divisa in quattro gruppi di accordi. C diventa una cadenza iv–V7, A+ e A++ salgono di un grado alla volta (in La minore: Am → Bm → Cm). Per esempio A B A C su 8 battute: `Am F | C G | Am F | Dm E7`.

Le lettere delle sezioni compaiono sul piano roll.

### GENERATE (lead)
Scegli Style, Key, Scale e Bars.
- **Stili**:
  - Pursuit, Hate or Glory, Opr, Dark Arp, Acid Slide;
  - *Gallop*: croma + due semicrome, molto sulla radice;
  - *Rave Stab*: colpi sincopati con ottave sui levare.
- **Scale**:
  - Natural Minor, Phrygian, Harmonic Minor, Phrygian Dominant, Dorian, Locrian, Hungarian Minor;
  - Double Harmonic, Neapolitan Minor, Aeolian b5, Minor Pentatonic.
- **Humanize**: micro-variazioni di timing (±1/64 di beat) e velocity, sempre uguali per lo stesso seed. Le legature dei glide restano intatte.

### TRANSFORM (voicing)
Trascina un `.mid` sulla finestra del plugin, oppure usa **LOAD MIDI** o **CAPTURE** (armi, suoni sulla traccia A, premi di nuovo). Anche un `.mid` MPE polifonico va bene: con *Keep Expr* l'espressione di chi suona viene conservata.

Voicing disponibili: Drop 2, Open Spread, Dark Cluster, Quartal, Power + Oct, Add 9+11, Unison Stack, Epic Spread, Gothic e Hyper Spread.

### CINEMATIC (armonie)
Senza MIDI caricato suona una **Progression**, sempre nella Key scelta. Nella barra di stato vedi gli accordi che stanno suonando (es. `Am(maj7)  F(add9)  A#/A`).

| Progression | Accordi (in La) |
|---|---|
| Epic Minor | Am F C G |
| Phrygian Dark | Am A# Gm A# |
| Harmonic Dominant | Am F Dm E7 (con la sensibile) |
| Lament Bass | Am Em/G Dm/F E (basso che scende) |
| Mediant Chain | Am Fm Am C#m (mediante cromatiche) |
| Tritone Abyss | Am D#m A# E |
| Tonic Pedal | Am A#/A G/A F/A |
| Line Cliche | Am Am(maj7) Am7 Am6 |
| Neapolitan | Am A#/D E7 Am |
| Andalusian Dark | Am G F E |
| Auto (Seed) | sequenza generata dal seed: più **Darkness**, più cromatismi, tritoni e bII |

Se carichi i tuoi accordi, vengono trasformati quelli.

**Chord Length** (2 beat, 1 bar, 2 bar) è la durata di ogni accordo.

Colore:
- **Tension** aggiunge note di colore a ogni accordo;
- **Darkness** decide quali: 9, 11, 6, maj7, oppure b9, b13, m(maj7), #11.

**Reharmonise**:
- *Sus Resolve*: la 4ª scende sulla 3ª.
- *Chromatic Approach* e *Tritone Approach*: l'ultimo tempo è l'accordo successivo mezzo tono sopra, oppure a un tritono, e ci scivola dentro.
- *Mediant Shift*: la seconda metà di ogni accordo si sposta a una mediante cromatica.
- *Suspensions*: le voci alte entrano un grado sopra (4-3, 9-8, b6-5) e risolvono con il bend.
- *Tonic Pedal*: il basso resta sulla tonica.
- *Planing*: tutti gli accordi prendono la forma del primo (armonia parallela).

**Motion**:
- *Morph*: ogni voce è una nota lunga che scivola da un accordo all'altro.
- *Bloom*: ogni accordo si apre a ventaglio da un unisono.
- *Collapse*: ogni accordo si richiude nell'unisono.
- *Breathe*: prima si apre, poi si richiude.
- *Deep Note*: il primo accordo nasce da un cluster di voci che vagano e poi convergono, come il THX.
- *Pulse*: accordi ribattuti a semicrome (quanti colpi lo decide **Pulse**) che scivolano al cambio d'accordo.
- *Tension Rise*: il basso scende e le voci alte salgono, sempre più veloci, fino al cambio.

**Voicing**:
- *Epic Spread*: su 3 ottave, con i colori in alto.
- *Gothic*: m3 e maj7 al centro, b9 in cima.
- *Hyper Spread*: su 4 ottave.
- In più tutti gli altri voicing di TRANSFORM.

Il resto:
- **Glide / Anticipate / Stagger**: durata del glide, quanto arriva in anticipo sul cambio d'accordo e sfalsamento tra le voci.
- **Glide Shape**: Linear, Ease, Swoop In, Swoop Out, e *Stepped*, che fa il glide a gradini sulle note della scala come un glissato di ottoni.
- **Swell**: crescendo di pressure e timbro su ogni accordo, con un vibrato lento sulla voce più acuta.
- **Arc**: crescendo su tutta la frase.
- **Fall**: cadute di pitch a fine accordo.
- **Sub**: una voce in più, un'ottava sotto il basso.
- **Voices**: fino a 8 voci.

### KIT (una traccia intera)
Sei layer costruiti su Key, Scale, progressione (quella dello Style) e seed comuni, quindi suonano insieme. Ogni layer ha **ON**, **Pattern**, **Density**, **Octave** e, se suona una linea sola, **MONO**.

| Layer | Pattern |
|---|---|
| Lead | il lead di GENERATE |
| Bass | Rolling (le tre semicrome dopo il beat), Offbeat, Pedal + Oct, Arp Down (arpeggio sul basso); scivola nella battuta successiva |
| Arp | Up, Down, Up Down, Random sulle note dell'accordo, su due ottave |
| Siren | Rise (sale di un'ottava), Wail, Fall (tuffo da +12), Alarm (terza minore in crome): bend MPE per nota |
| Stab | accordi di potenza sugli accenti del lead, in levare, sincopati o sul battere, con una caduta di pitch |
| Pad | il motore CINEMATIC sugli accordi del kit (usa le impostazioni di CINEMATIC) |

- Il **layer a fuoco** (clicca sul nome) è disegnato sopra gli altri nel piano roll, va all'uscita MIDI dell'host e si vede nel monitor.
- **EXPORT** scrive un file `"<nome> - <Layer>.mid"` per layer.
- **DRAG** trascina tutti i layer insieme: Live crea una traccia per file.

## Uscite (pannello OUTPUT)
- **Port Out**: le porte MIDI virtuali (vedi sotto).
- **MPE Bend**: bend range per nota dell'uscita MPE (default 48).
- **Mono Lead / MONO dei layer**: una linea sola sul canale 1 con pitch bend di canale, per i synth senza MPE. **Mono Bend** (default 12) deve coincidere con il bend range del synth.
  - Un `.mid` mono trascinato in Live mantiene i glide, come envelope di Pitch Bend della clip.
  - Nel KIT il Bass è mono di default.
- **Key Trigger**: le note che arrivano sulla traccia di DarkMPE comandano il loop.
  - *Transpose*: l'ultima nota suonata trasporta tutto rispetto alla Key (ripiegato tra −5 e +6, così il registro non salta) e resta anche dopo il rilascio. Con una clip di note-radice sulla traccia A, il lead segue gli accordi del brano.
  - *Gate*: il loop suona solo finché tieni premuto un tasto e riparte dall'inizio nel momento esatto in cui lo premi, anche a transport fermo.

## Preset
**PRESET ▾** contiene 30 preset di fabbrica (Lead, Cinematic, Kit, Transform; alcuni usano le Phrase Form, come *ABAC Anthem*, *Rising Sequence*, *ABAC Track*), che sono anche i Program dell'host.
- I preset utente si salvano con *Save preset…* in `~/Music/DarkMPE/Presets` (file `.dmpreset`, parametri + seed).
- Un preset non tocca mai le impostazioni di uscita: porte, bend range, Key Trigger e Mono Lead.

La finestra si ridimensiona dall'angolo in basso a destra, oppure dal menu **100%** (60–160%). La dimensione viene salvata nel progetto.

## MPE vero in Live 12: le porte "DarkMPE"
**Ableton Live importa i `.mid` senza MPE**: fonde i canali in un'unica curva di Pitch Bend della clip. Lo stesso vale per l'export di Live, che non scrive l'MPE. Il `.mid` MPE di DRAG/EXPORT funziona invece in Bitwig, Logic, Cubase e Reaper.

In Live l'MPE nativo entra solo da una porta di input con MPE Mode, mentre il routing "MIDI From: traccia" tra tracce appiattisce tutto in un pitch bend globale. Per questo il plugin pubblica delle porte MIDI virtuali.

1. **Traccia A**: DarkMPE, con input *No Input* (oppure il tuo controller se usi Key Trigger).
2. **Live → Settings → Link, Tempo & MIDI**: sulla riga di Input **DarkMPE Out** attiva **Track** e **MPE**.
3. **Traccia B**: il synth (Serum 2, Drift, Wavetable, Meld…).
   - MIDI From = **DarkMPE Out**, Monitor = **In**.
   - Per i plugin (Serum): tasto destro sul titolo del device → **Enable MPE Mode**; dentro Serum attiva l'MPE con bend range 48.
4. Premi Play in Live (oppure PREVIEW nel plugin): il synth riceve l'MPE per nota. La striscia **MPE OUT** sotto il piano roll mostra, canale per canale, nota, bend, slide e pressure in uscita.
5. **Clip MPE**: arma la traccia B e registra. La clip ha il pitch per nota nel tab *MPE* (Note Expression).

### Una istanza, più synth
- **La stessa parte su più synth (layering)**: metti `MIDI From = DarkMPE Out` (Monitor In, MPE attivo sul synth) su più tracce. Ognuna riceve lo stesso flusso MPE; per registrare le armi tutte.
- **Parti diverse su synth diversi**: usa il **KIT**. Ogni layer ha la sua porta:
  - **DarkMPE Out** (Lead), **DarkMPE Bass**, **DarkMPE Arp**, **DarkMPE Siren**, **DarkMPE Stab**, **DarkMPE Pad**;
  - una porta appare la prima volta che il layer viene acceso in KIT e poi resta, così Live conserva il routing;
  - nelle impostazioni MIDI di Live attiva Track + MPE una volta per porta (per i layer MONO basta Track), poi punta ogni traccia-synth alla sua porta. Tutto resta a tempo e sulla stessa armonia, anche col Key Trigger.
- Con più istanze le porte si chiamano "DarkMPE Out 2", "DarkMPE Bass 2" e così via.

Per capire come viene letto un file MIDI:
```bash
./build/DarkMPETests_artefacts/Release/DarkMPETests --inspect "file.mid"
```

Per controllare l'interfaccia senza aprire una DAW (su Linux: `xvfb-run -s "-screen 0 2560x1600x24"`):
```bash
./build/DarkMPEProcessorTests_artefacts/Release/DarkMPEProcessorTests --snapshot screenshots
```

## Struttura
- `Source/engine/PhraseForm`: le forme di frase (call and response) e le loro sezioni.
- `Source/engine/HarmonyEngine`: progressioni (anche divise per forma), colori (Tension/Darkness), nomi degli accordi.
- `Source/engine/CinematicEngine`: regioni di accordi, reharm, voicing a slot, movimenti (Morph, Bloom, Collapse, Breathe, Deep Note, Pulse, Tension Rise), sospensioni, fall, arc.
- `Source/engine/KitGenerator`: i sei layer del KIT.
- `Source/engine/MelodyGenerator`: ritmo euclideo o fisso, motivo, pedale, ottave, cromatismi e slide.
- `Source/engine/VoicingEngine`: rilevamento degli accordi, revoicing, voice leading a movimento minimo, strum.
- `Source/engine/ExpressionShaper`: glide, detune, vibrato, curve di timbro e pressure (punti fitti dove il pitch si muove veloce).
- `Source/engine/Humanize`: timing e velocity.
- `Source/engine/MpeRenderer`:
  - uscita MPE: allocazione dei canali della Lower Zone (2-16), PB ±48;
  - uscita mono sul canale 1;
  - campionamento adattivo delle curve: glide lisci e pochi eventi dove il valore è fermo.
- `Source/engine/MidiFileIO`: import ed export `.mid` (960 PPQ).
- `Source/PortHub`: porte MIDI virtuali alimentate dall'audio thread tramite una FIFO lock-free.
- `Source/presets/Presets`: preset di fabbrica e utente.
- `Source/PluginProcessor`: parametri, rebuild, riproduzione a loop in tempo reale (senza allocazioni), Key Trigger, cronologia dei seed.
- `Source/PluginEditor`, `Source/ui`: interfaccia scalabile, piano roll con immagine in cache, monitor MPE.
