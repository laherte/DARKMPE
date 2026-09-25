# DarkMPE

Generatore MPE in stile Gesaffelstein: lead, armonie cinematiche, tracce complete a layer e trasformazione di MIDI in voicing MPE. Formati VST3, AU e Standalone, pensato per Ableton Live 12.

## Build
```bash
cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build -j8
ctest --test-dir build        # test del motore e del processor (MPE valido, zero allocazioni sull'audio thread)
```
Per copiare i plugin in `~/Library/Audio/Plug-Ins` dopo la build aggiungi `-DDARKMPE_INSTALL=ON` alla configurazione, oppure copia a mano da `build/DarkMPE_artefacts/Release/{VST3,AU}`.

Demo pronte all'uso: ogni stile di lead (MPE e Mono), ogni profilo di Gesture, le Phrase Form del lead, 13 vetrine CINEMATIC, un KIT completo (un file per layer), e ogni voicing e versione cinematica dei `.mid` in `Examples/`:
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

### PHRASE FORM (linee melodiche)
Il selettore **PHRASE FORM** in alto decide la struttura della frase delle linee melodiche: il lead, il basso e l'arp. *Classic* è la generazione di sempre; le altre forme sono strutture di call and response prese dal songwriting e dalla teoria della frase:

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
- La voce che si muove è scritta sui gradi della tonalità, quindi **la stessa lettera torna nota per nota anche se sotto c'è un altro accordo**. Il pedale (la nota ribattuta dello stile) segue invece l'accordo della battuta.
- Anche l'espressione torna uguale: humanize, detune, vibrato e gesti MPE di A si ripetono identici. **MUTATE** cambia le risposte e non tocca mai A.
- **B** tiene il ritmo e l'inizio di A, specchia il profilo (anche i gesti: un dip diventa un lift) e finisce aperta sulla **quinta della tonalità**.
- **B'** e **C** chiudono sulla **tonica della tonalità**. C riprende l'inizio di A, poi scende per grado fino alla tonica e la tiene, con un vibrato più largo.
- **F** ripete la testa di A, un grado più su.
- Una sezione si sposta di ottava tutta insieme per stare nel registro, quindi A+, A++ e A' conservano il loro profilo.

Dove si applica:
- **GENERATE**: il lead.
- **KIT**: il lead, l'arp (le risposte girano al contrario, la chiusura scende a casa) e il basso (ottava sulla risposta, fill che cammina fino alla tonica sulla cadenza).
- **Gli accordi non cambiano mai con la forma.** In CINEMATIC il selettore è spento; nel KIT Pad e Stab seguono la progressione.

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

### MPE EXPRESSION: gesti, vibrato, detune
Oltre al glide da una nota all'altra, ogni nota può avere un suo **gesto**: un movimento di pitch, timbro (CC74) e pressure che solo l'MPE rende possibile, perché ogni nota ha il suo canale.

**Gesture** sceglie il profilo; **Amount** quante note ricevono un gesto; **Depth** quanto può essere grande (da 1 semitono a un'ottava); **Bend Riff** quanto spesso le frasi corte diventano una nota sola.

| Gesto | Cosa fa |
|---|---|
| Dip / Lift | scende (o sale) di un grado della scala, 1 o 2 semitoni, e torna |
| Scoop | la nota entra da sotto (da sopra nelle risposte della forma) |
| Fall | a fine nota cade di qualche grado, fino a un'ottava con Depth al massimo |
| Approach | si piega solo in parte verso la nota successiva, poi arriva la nota vera |
| Overshoot | il glide supera la nota e si assesta |
| Stepped Glide | il glide passa per le note della scala |
| Trill | trillo col grado sopra, fatto di bend |
| Wobble | LFO di pitch a tempo (crome, terzine, semicrome) con il timbro che lo segue |
| Dive | picchiata verso il basso (un'ottava sul basso) |
| **Bend Riff** | 2–4 note corte dentro un beat diventano **una nota sola** che fa la melodia col bend; gli attacchi restano come colpi di timbro e di pressure |

Su timbro e pressure: *pluck*, *wah* a tempo, sequenza di CC74 a 16 step, tremolo di pressure a biscrome, *swell*.

| Profilo | Carattere |
|---|---|
| Classic | solo i glide, come prima |
| Liquid | Bend Riff morbidi, approach, stepped glide, wah |
| Vocal | scoop, fall, overshoot leggeri, swell e più vibrato sulle note lunghe |
| Acid | overshoot, approach, sequenza di CC74 e pluck |
| Aggressive | fall, dive, overshoot, pluck |
| Glitch | trilli, wobble, tremolo di pressure, Bend Riff secchi |
| Auto | sceglie in base allo Style (Acid Slide → Acid, Opr e Rave Stab → Glitch, Dark Arp → Liquid, gli altri → Aggressive) |

- I gesti rispettano la scala: il dip di La in Frigio scende a Sol (2 semitoni), quello di Sib a La (1).
- Il **basso** del KIT fa scoop, fall, dive e riff d'ottava; l'**arp** riff e dip; gli **Stab** (e gli accordi trasformati) muovono tutte le voci dell'accordo insieme, sfalsate: scoop, fall, dive o *rip* verso l'alto.
- **Vibrato**: il ritardo e l'attacco si adattano alla lunghezza della nota. Canta ogni nota che ha almeno 3/4 di ciclo dopo il ritardo (al rate di default, già le note da un beat); le semicrome no.
- **Detune**: nelle linee singole ogni nota ha un suo scarto (fino a metà del valore) e una deriva lenta, analogica; negli stack le voci si allargano.
- **Si vede tutto**:
  - nel piano roll la corsia **BEND** mostra il bend in cent vicino alla nota (detune, deriva, vibrato) e in semitoni oltre (glide, gesti);
  - l'anteprima in fondo al pannello modella una frase dimostrativa con le impostazioni correnti e si aggiorna mentre giri i knob;
  - il monitor MPE OUT ha una barra fine di ±1 semitone e il valore in cent.

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
| Auto (Seed) | generata dal seed con una grammatica funzionale: parte dalla tonica, passa per le predominanti e chiude su una dominante (da 8 accordi due frasi, con semicadenza a metà); più **Darkness**, più cromatismi (bII, bvi, #iv, vii dim, V7); alcuni rivolti fanno camminare il basso per grado |

Se carichi i tuoi accordi, vengono trasformati quelli.

**Chord Length** (2 beat, 1 bar, 2 bar) è la durata di ogni accordo.

Colore:
- **Tension** aggiunge note di colore a ogni accordo;
- **Darkness** decide quali: 9, 11, 6, maj7, oppure b9, b13, m(maj7), #11.
- Un accordo che ritorna riceve gli stessi colori.

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
- *Counterline*: come Morph, e la voce acuta canta una contromelodia di gradi della scala dentro ogni accordo.
- *Ripple*: come Morph, e le voci fanno un dip di un grado una dopo l'altra, dal basso verso l'alto.
- *Shimmer*: come Morph, e le voci si allargano di qualche cent con un vibrato ciascuna, poi si ricompattano sul cambio.

Il loop gira senza giunture: il primo accordo è voiced a partire dall'ultimo, e a fine loop le voci scivolano già nel primo accordo.

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
| Stab | accordi di potenza sugli accenti del lead, in levare, sincopati o sul battere, con una caduta di pitch e i gesti d'accordo del profilo |
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
**PRESET ▾** contiene 35 preset di fabbrica (Lead, Cinematic, Kit, Transform; alcuni usano le Phrase Form, come *ABAC Anthem* e *ABAC Track*, altri un profilo di Gesture, come *Liquid Bend Riffs* e *Glitch Trills*), che sono anche i Program dell'host.
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
- `Source/engine/HarmonyEngine`: progressioni (Auto con grammatica funzionale), colori (Tension/Darkness), nomi degli accordi.
- `Source/engine/CinematicEngine`: regioni di accordi, reharm, voicing a slot senza giunture al loop, movimenti (Morph, Bloom, Collapse, Breathe, Deep Note, Pulse, Tension Rise, Counterline, Ripple, Shimmer), sospensioni, fall, arc.
- `Source/engine/GestureEngine`: i gesti MPE (profili, Bend Riff, articolazioni di timbro e pressure, gesti d'accordo).
- `Source/engine/CurveShapes`: forme dei glide e transizioni condivise (anche Stepped).
- `Source/engine/KitGenerator`: i sei layer del KIT.
- `Source/engine/MelodyGenerator`: ritmo euclideo o fisso, motivo, pedale, ottave, cromatismi e slide.
- `Source/engine/VoicingEngine`: rilevamento degli accordi, revoicing, voice leading a movimento minimo, strum.
- `Source/engine/ExpressionShaper`: glide, detune con deriva, vibrato, curve di timbro e pressure, e i gesti sopra (punti fitti dove il pitch si muove veloce).
- `Source/engine/Humanize`: timing e velocity.
- `Source/engine/MpeRenderer`:
  - uscita MPE: allocazione dei canali della Lower Zone (2-16), PB ±48;
  - uscita mono sul canale 1;
  - campionamento adattivo delle curve: glide lisci e pochi eventi dove il valore è fermo.
- `Source/engine/MidiFileIO`: import ed export `.mid` (960 PPQ).
- `Source/PortHub`: porte MIDI virtuali alimentate dall'audio thread tramite una FIFO lock-free.
- `Source/presets/Presets`: preset di fabbrica e utente.
- `Source/PluginProcessor`: parametri, rebuild, riproduzione a loop in tempo reale (senza allocazioni), Key Trigger, cronologia dei seed.
- `Source/PluginEditor`, `Source/ui`: interfaccia scalabile, piano roll con immagine in cache e corsia BEND, monitor MPE, anteprima dell'espressione.
