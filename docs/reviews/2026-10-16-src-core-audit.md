# Core audit for cross subsystem follow up

## Auftrag und Ergebnis

Die Fragen des Operators, wörtlich:

1. „sollten wir core aufräumen?“
2. „anders organisieren?“
3. „hat die implementierung einen durchgenden stil?“
4. „gibt es offene punkte, bugs, implementierungs gaps?“

Der Folgeauftrag lautet: „Erstelle einen Bericht aus deinen Ergebnissen in IntrinsicEngine so dass Codex 6 Astra Ultra anschließend alles finden kann. Ich lasse für alle weiteren unterordner von IntrinsicEngine/src von anderen Agenten ebenfalls so einen Bericht erstellen“

**Ergebnis:** gezieltes Aufräumen ist sinnvoll; ein großer Verzeichnisumbau ist
derzeit nicht begründet. Vorrang haben Dateispeicherung, Datenrennen und
nachgewiesene API-Fehler. Danach folgen ungenutzte Telemetrie-Infrastruktur und
die Entscheidung über zwei teilweise doppelte Graph-Compiler. Der Grundstil
ist ähnlich, die Verträge und Dokumentation sind nicht durchgehend konsistent.

Dieser Bericht dient als selbständige Übergabe für die spätere Gesamtauswertung.
Er autorisiert keine Implementierung, API-Entfernung oder Änderung bereits
vereinbarter Operatorentscheidungen. Die Befund-IDs `CORE-A01` bis `CORE-A13`
entsprechen der Nummerierung der Audit-Antwort; sie sind **keine Task-IDs**.

## Bezugsstand und Abdeckung

| Feld | Wert |
|---|---|
| Datum | 2026-10-16, Europe/Berlin |
| Repository | IntrinsicEngine |
| Untersuchte Revision | `66da33fd6bfabb20408607b8361354fc9adb420d` |
| Primärer Bereich | `src/core` einschließlich README und CMake |
| Angenommene Nutzung | Ein Editorprozess mit Worker-Threads, asynchronem Assetladen, Frame-Maintenance und paralleler Renderaufzeichnung |
| Verfahren | Ponytail-Audit; drei unabhängige Rollenprüfungen, anschließend Quellenabgleich und gezielte Reproduktionen durch den Koordinator |
| Änderungen am Enginecode | Keine |
| Zielbezug | Ausdrücklich vom Operator gewählte Qualitätsprüfung der gemeinsamen Enginebasis; kein separates Goal-Dokument gefunden |
| Nicht erhoben | Performancegewinne, Benchmarkresultate, Operational-/GPU-Reife oder Forschungsclaims |

| Rolle | Gelesener Schwerpunkt und verbundene Pfade |
|---|---|
| Runtime-Integrator | Alle `Core.Tasks*`, Queue, DAG, TaskGraph und FrameGraph; zugehörige Tests; Engine-/JobService-Shutdown, RenderGraph-Ausführung und RenderPrep |
| Geometry-/Asset-Entwickler | Memory, ResourcePool, Handles, Leases, CallbackRegistry, beide Heaps, Hash, Base64, IntegerMath und Geometry2D; Aufrufer und Grenzfalltests |
| Anwendungsintegrator und Betreiber | Config, Filesystem, IOBackend, Process, Logging, Telemetry, FrameClock, Error und Formatkatalog; Szenenspeicherung und Rendereraufrufer |
| Koordinator | Architektur, Stil, Dokumentation, Testauswahl, Reproduktionen und Abgleich der unabhängigen Befunde |

Die Rollenagenten arbeiteten ohne Änderungen und ohne weitere Delegation;
bei diesen Subagenten waren die Beschränkungen instruktionsbasiert. Nicht jeder
verbundene Enginepfad wurde vollständig gelesen. Insbesondere ist die
Parallelitätsanalyse keine dynamische TSan- oder Vulkan-Verifikation.

Alle Zeilennummern unten beziehen sich auf die genannte Revision. Die verlinkten
Dateien und genannten Symbole bleiben die Suchanker, wenn sich Zeilen verschieben.
Vor einer Reparatur die aktuelle Revision und aktuelle Aufrufer erneut prüfen.

## Befundübersicht

P1 bedeutet hier: zuerst bearbeiten, wegen Datenverlust oder Datenrennen auf
zugesicherten beziehungsweise produktiv erreichbaren Pfaden. P2 bezeichnet
konkrete API-/Testfehler mit begrenzterer nachgewiesener Produktionsreichweite.
P3 bezeichnet Diagnose- und Bereinigungsarbeit. Dies ist keine Behauptung, jeder
P1 sei bereits als laufender Editorabsturz beobachtet worden.

| ID | Prio | Befund | Evidenz |
|---|---|---|---|
| CORE-A01 | P1 | Dateischreiben meldet Erfolg vor geprüftem Schreibabschluss | Laufzeitreproduktion; Szenenspeicher-Aufrufer verfolgt |
| CORE-A02 | P1 | Telemetrie-Kategorien sind nicht threadsicher | Quellbeweis und paralleler Renderer-Aufrufpfad; kein TSan-Lauf |
| CORE-A03 | P1 | ResourcePool liest Löschliste außerhalb des Locks | Quellbeweis; Überschneidung nicht dynamisch reproduziert |
| CORE-A04 | P2 | LinearArena richtet Offset statt tatsächlicher Adresse aus | Laufzeitreproduktion |
| CORE-A05 | P2 | Clear und erneutes Add lassen alte Pool-Handles wieder gültig werden | Laufzeitreproduktion; Wiederverwendungs-Vertrag klären |
| CORE-A06 | P2 | Prozess-Timeout geht nach Pipe-Ende verloren | Laufzeitreproduktion |
| CORE-A07 | P2 | Raw-DAG akzeptiert expliziten Selbstzyklus | Laufzeitreproduktion; Gegenvertrag in TaskGraph vorhanden |
| CORE-A08 | P2 | Shutdown verwirft queued Coroutine-Frames ohne Zerstörung | Besitzkette statisch geprüft; kein Laufzeitrepro |
| CORE-A09 | P2 | Dispatch akzeptiert keine benannte kopierbare Lambda | Compilerreproduktion |
| CORE-A10 | P2 | StringID-Ordnung und Gleichheit widersprechen sich im Debug-Build | Laufzeitreproduktion |
| CORE-A11 | P2 | CallbackRegistry-Nebenläufigkeitstest hat eigenes Datenrennen | Quellbeweis |
| CORE-A12 | P3 | Telemetrie-Samplepuffer wird geschrieben, aber nie gelesen | Gesamtsuche; tatsächliche Strukturgröße geprüft |
| CORE-A13 | P3 | Diagnose-APIs liefern falsche Einheiten oder verlieren Werte | Quellbeweis; keine produktiven Leser gefunden |

## CORE-A01 Dateischreiben bestätigt ungeprüften Abschluss

- **Ort:** [Core.IOBackend.cpp](../../src/core/Core.IOBackend.cpp), Zeilen 55–77,
  `FileIOBackend::Write`.
- **Mechanismus:** Öffnen mit `std::ios::trunc`, Prüfen von `write()`, danach
  `Core::Ok()` ohne Auswertung von `flush()` oder `close()`. Kleine Writes können
  zunächst nur im Streambuffer liegen. Ein Fehler beim abschließenden Schreiben
  wird dann nicht zurückgegeben; der alte Dateiinhalt ist bereits abgeschnitten.
- **Reproduktion:** ein Byte nach `/dev/full` schreiben. Erwartung: Fehler.
  Beobachtung: `has_value() == true`; die Assertion auf Fehler schlägt fehl.
- **Reichweite:** [Runtime.SceneSerialization.cpp](../../src/runtime/Scene/Runtime.SceneSerialization.cpp),
  Zeile 3025, vertraut auf `backend.Write`. Der Save-Pfad in
  [Runtime.SceneDocumentModule.cpp](../../src/runtime/Scene/Runtime.SceneDocumentModule.cpp),
  Zeilen 846–853, markiert die Szene nach Erfolg als gespeichert.
- **Kleinster vollständiger Reparaturansatz:** Schreibabschluss explizit prüfen.
  Für das Ersetzen bestehender Szenendateien eine benachbarte temporäre Datei
  vollständig schreiben und erst dann ersetzen. Die Verantwortung dafür mit dem
  Szenenspeichervertrag abstimmen; nicht ungeprüft atomisches Ersetzen für jede
  beliebige `IIOBackend::Write`-Nutzung verlangen.
- **Regression:** gepufferter Schreibfehler muss zurückgegeben werden; ein
  fehlgeschlagenes Ersetzen darf die vorherige Szenendatei nicht zerstören.
  Linux-spezifische `/dev/full`-Prüfung von portabler Fehler-Injektion trennen.
- **Ohne Fix:** falsch bestätigtes Speichern und möglicher Verlust des alten Stands.

## CORE-A02 Telemetrie hat Datenrennen

- **Orte:** [Core.Telemetry.cpp](../../src/core/Core.Telemetry.cpp),
  `TimingCategory::AddSample` (49–54), `RecordSample` (149–164),
  `FindOrCreateCategory` (298–312); Thread-Versprechen in
  [Core.Telemetry.cppm](../../src/core/Core.Telemetry.cppm), Zeilen 174–176.
- **Mechanismus:** `TotalTimeNs`, `CallCount`, Min und Max sind gewöhnliche
  gemeinsam beschriebene Felder. Registrierung erhöht den sichtbaren Zähler vor
  der Initialisierung des Eintrags. Zwei Registrierer nahe 256 Kategorien können
  den Zähler über die Kapazität erhöhen; spätere Scans/Resets begrenzen ihn nicht.
- **Produktiver Pfad:** [Graphics.Renderer.cpp](../../src/graphics/renderer/Graphics.Renderer.cpp),
  Zeilen 3497–3530, ruft `ExecuteParallelRecordJoin` mit aktiviertem Scheduler auf.
  Die Worker führen `passContext.Begin()/End()` aus. In
  [Backends.Vulkan.CommandContext.cpp](../../src/graphics/vulkan/Backends.Vulkan.CommandContext.cpp),
  Zeilen 171 und 209, erfassen `ScopedTimer` dieselben Kategorien.
- **Evidenzgrenze:** Quellen belegen den gemeinsamen Schreibpfad. Keine verlorenen
  Zählerstände, Arrayüberschreitung oder Abstürze unter TSan/Vulkan reproduziert.
  Produktiv gibt es Schreiber, aber keine Kategorie-Leser und keine
  Frame-Resets dieser Telemetrie (siehe CORE-A12). Die aktuelle nachgewiesene
  Wirkung ist damit undefiniertes Verhalten im Schreibpfad, keine beobachtete
  falsche Telemetrieanzeige. Das Kapazitätsproblem setzt mehr verschiedene gleichzeitige Kategorien voraus
  als der hier verfolgte typische Rendererpfad.
- **Reparatur:** Kategorienregistrierung, Updates, Reset und Leser mit einem
  konsistenten Synchronisationsvertrag versehen. Eine neue lockfreie Registry
  ist nicht notwendig. Rückgegebene Referenzen/Pointer bei der Leserstrategie
  beachten; ein isolierter Lock nur in `AddSample` reicht nicht für alle Pfade.
- **Regression:** mehrere Worker schreiben dieselbe Kategorie mit exakt
  erwartbarer Anzahl; zusätzliche Kapazitätsgrenze und Registrierung gegen
  Beobachtung testen. Vorhandene Profiling-Tests sind sequentiell.
- **Ohne Fix:** undefiniertes Nebenläufigkeitsverhalten und unzuverlässige Telemetrie.

## CORE-A03 ResourcePool Maintenance liest ohne Lock

- **Ort:** [Core.ResourcePool.cppm](../../src/core/Core.ResourcePool.cppm),
  `ProcessDeletions`, Zeilen 87–92; konkurrierendes `Remove`, Zeile 82.
- **Mechanismus:** `m_PendingKill.empty()` liegt vor `std::unique_lock`.
  `Remove()` verändert denselben Vektor unter dem Mutex. Der dokumentierte
  einzelne Maintenance-Thread schließt andere aufrufende Threads nicht aus.
- **Reichweite:** Null- und Vulkan-Backends verwenden Frame-Maintenance und
  Ressourcenentfernung. Eine tatsächliche zeitliche Überschneidung wurde nicht
  ausgeführt; der zugesicherte Pool-Vertrag erlaubt sie.
- **Reparatur:** bestehenden Lock vor die Abfrage setzen.
- **Regression:** paralleles Entfernen und Maintenance; TSan ist hier ein
  geeigneter Nachweis. `CoreResourcePool.ConcurrentReadsAreSafe` deckt diesen
  Fall nicht ab.
- **Ohne Fix:** Datenrennen trotz der dokumentierten Synchronisationsgarantie.

## CORE-A04 LinearArena erfüllt größere Ausrichtungen nicht

- **Ort:** [Core.Memory.LinearArena.cpp](../../src/core/Core.Memory.LinearArena.cpp),
  Basisspeicher Zeile 58, `AllocBytes` Zeilen 134–145.
- **Mechanismus:** die Basis ist nur auf 64 Byte ausgerichtet; `AlignUp(m_Offset,
  safeAlign)` berücksichtigt ihre tatsächliche Adresse nicht.
- **Reproduktion:** ein Arena-Speicherblock lag im Probeprogramm bei Rest 64
  modulo 128. `AllocBytes(1, 128)` meldete Erfolg und lieferte dieselbe falsche
  Ausrichtung. Das generische `New<alignas(128) T>()` könnte dort ein Objekt mit
  ungültiger Ausrichtung konstruieren.
- **Reichweite:** öffentliche generische API. Die untersuchten produktiven
  EPA-Typen benötigen geringere Ausrichtung; kein heutiger Rendering-/EPA-Absturz
  wird daraus behauptet.
- **Reparatur:** tatsächliche aktuelle Adresse und Restkapazität ausrichten,
  beispielsweise mit `std::align`; Argumentvalidierung und Überlaufprüfungen
  erhalten. Angeforderte Ausrichtung vor einer Normalisierung validieren.
- **Regression:** mehr als Cacheline-Ausrichtung testen. Der Fehler hängt von
  der Basisadresse ab; ein zufällig passend ausgerichteter Block ist kein
  Gegenbeweis. Der reproduzierte konkrete Block erfüllte die Fehlerbedingung.
- **Ohne Fix:** undefiniertes Verhalten für zulässige überausgerichtete Typen.

## CORE-A05 Clear verliert die Generationengeschichte

- **Ort:** [Core.ResourcePool.cppm](../../src/core/Core.ResourcePool.cppm),
  `Clear` Zeilen 109–114 und `Add` Zeilen 57–62.
- **Reproduktion:** `old = Add(1); Clear(); fresh = Add(2);` ergibt
  `old == fresh` und `Get(old)` liefert die neue Ressource 2.
- **Vertragsfrage:** Falls `Clear()` ausschließlich das endgültige Ende der
  Pool-Lebensdauer bedeuten soll, muss das dokumentiert und durch die API
  durchgesetzt werden. Aktuell bleibt der Pool benutzbar, beschreibt generational
  stale-handle rejection und testet die Ungültigkeit alter Handles nach Clear.
- **Reichweite:** aktuelle produktive Clear-Aufrufe liegen beim Vulkan-Shutdown.
  Eine Reinitialisierung mit überlebenden alten Handles wurde nicht nachgewiesen.
- **Reparatur bei wiederverwendbarem Pool:** Generationen auch nach Clear
  eindeutig halten; dafür bestehende Slots oder einen einfachen Poolzähler
  verwenden, kein neues Handle-Framework.
- **Regression:** vorhandenen Clear-Test um erneutes Add erweitern; alte Handles
  müssen danach weiterhin ungültig sein. Kapazitätsvertrag gleichzeitig beachten.
- **Ohne Fix:** alte Identitäten können unbemerkt neue Ressourcen adressieren.

## CORE-A06 Prozess-Timeout endet mit den Ausgabepipes

- **Ort:** [Core.Process.cpp](../../src/core/Core.Process.cpp), Zeilen 238–242,
  `Run` und `WaitForProcess`.
- **Reproduktion:** `sh -c 'exec 1>&- 2>&-; sleep 1'`, `Timeout = 50ms`,
  Standard `CaptureOutput = true`. Erwartung: `TimedOut`. Beobachtung:
  `TimedOut == false`, `ExitCode == 0`; der Prozess wird bis zum Ende abgewartet.
- **Mechanismus:** nach dem Drain beider Pipes wird für das verbleibende Warten
  Timeout 0 verwendet, was unbegrenzt bedeutet.
- **Reichweite:** derzeit testseitige Nutzung. **Nicht als ungenutzt löschen:**
  [RUNTIME-282](../../tasks/backlog/runtime/RUNTIME-282-headless-batch-cli.md)
  hält das Modul ausdrücklich für den Headless-CLI-Pfad vor.
- **Reparatur:** eine gemeinsame Deadline für Drain und Prozessende verwenden.
- **Regression:** [Test.CoreProcess.cpp](../../tests/integration/runtime/Test.CoreProcess.cpp)
  um früh schließende Ausgabe ergänzen; owning target ist
  `IntrinsicRuntimeIntegrationTests`, Suite `CoreProcess`.
- **Ohne Fix:** ein zugesichertes Zeitlimit kann unbegrenzt überschritten werden.

## CORE-A07 Zwei Graph-Compiler widersprechen sich bei Selbstzyklen

- **Ort:** [Core.Dag.Scheduler.cpp](../../src/core/Core.Dag.Scheduler.cpp),
  `addEdge` Zeilen 116–119 und Verarbeitung expliziter Abhängigkeiten 169–184.
- **Reproduktion:** `TaskPlanGraph` erhält einen Task mit gültiger ID `{1,1}`
  und derselben ID in `dependsOn`. `BuildPlan()` liefert erfolgreich einen Plan,
  obwohl ein Zyklus vorliegt.
- **Vergleich:** [Core.Dag.TaskGraph.cpp](../../src/core/Core.Dag.TaskGraph.cpp)
  lehnt explizite Selbstabhängigkeiten ab. Der zugehörige Test steht in
  [Test.Core.TaskGraphLegacy.cpp](../../tests/unit/core/Test.Core.TaskGraphLegacy.cpp),
  ab Zeile 194. Raw-DAG-Tests prüfen Mehrknotenzyklen, darunter den
  1024-Knoten-Zyklus `CoreGraphCompiler.LargeCycleDiagnosticIsBounded` in
  [Test.Core.GraphCompiler.cpp](../../tests/unit/core/Test.Core.GraphCompiler.cpp),
  ab Zeile 178. Der Ein-Knoten-Selbstzyklus fehlt.
- **Reichweite:** `CreateDagScheduler` und `CreateTaskPlanGraph` haben außerhalb
  von Core nur Testaufrufer; produktive Nutzer verwenden auch ihre Datentypen.
- **Reparatur:** explizite Selbstabhängigkeit bei der Validierung zurückweisen.
  Selbstkanten aus mehrfachen Ressourcenangaben desselben Tasks weiterhin
  unterdrücken; nicht pauschal jede `from == to`-Unterdrückung entfernen.
- **Regression:** Ein-Knoten-Zyklus für beide Raw-DAG-Fassaden.
- **Strukturfolgerung:** Topologie, Prioritätsordnung, längster Pfad und
  Zyklusdiagnostik sind in Scheduler und TaskGraph teilweise doppelt. Erst die
  benötigte API-Oberfläche entscheiden, dann Mechanismen zusammenführen.
  Kein weiterer universeller Graph-Layer als erster Schritt.

## CORE-A08 Queued Coroutine-Frames haben beim Verwerfen keinen Besitzer

- **Orte:** [Core.Tasks.Dispatch.cpp](../../src/core/Core.Tasks.Dispatch.cpp),
  `Dispatch(Job&&)` ab Zeile 35 und `Reschedule`; Shutdown in
  [Core.Tasks.Lifecycle.cpp](../../src/core/Core.Tasks.Lifecycle.cpp), ab Zeile 40.
- **Mechanismus:** Dispatch leert `Job::m_Handle`. Die Queue-Lambda enthält nur
  einen rohen `coroutine_handle` und ein `shared_ptr<atomic<bool>>`. Letzteres
  besitzt keinen Frame-Deleter. Die Zerstörung der Lambda zerstört keinen Frame.
  Shutdown räumt geparkte Fortsetzungen auf, nicht noch nie gestartete Jobs in
  den Queues. `final_suspend = suspend_never` hilft erst beim Abschluss.
- **Trigger:** ein einzelner Worker steckt in einer gewöhnlichen Aufgabe,
  dahinter wartet ein Coroutine-Job; Shutdown beginnt vor dessen Ausführung.
  Auch verworfene gequeue-te Yield-Resumes sind relevant.
- **Reichweite:** Coroutine-Verbraucher sind derzeit Tests/SLO. Reguläres
  Engine-Shutdown drainiert vorher lambda-basierte JobService-Arbeit.
  Kein reproduzierter Leak im normalen Engine-Shutdown behauptet.
- **Reparatur:** besitzender einmaliger Resume-Auftrag, der beim Verwerfen den
  Frame zerstört und die Ownership bei Ausführung korrekt weitergibt.
- **Regression, noch auszuführen:** blockierten einzelnen Worker koordinieren,
  queued Coroutine mit `shared_ptr` als **Funktionsparameter** anlegen und nach
  Shutdown dessen `weak_ptr` prüfen. Ein Sentinel im Coroutine-Body genügt nicht,
  weil `initial_suspend` dessen Konstruktion verhindert. Bestehender Test
  `SchedulerShutdownReclaimsParkedFrameWithoutResuming` prüft nur geparkte Frames.
- **Bestehender Entscheidungsowner:**
  [RUNTIME-325](../../tasks/backlog/runtime/RUNTIME-325-coroutine-waits-in-jobservice.md).
  Der Operator hat sofortige Entfernung zurückgestellt: erst einen echten
  JobService-Verbraucher auswählen und bestätigen, sonst Entfernung. Die dortige
  schrittweise Adoptionsregel bleibt bindend. Blocking CounterEvent und
  Worker-Parking sind nicht automatisch Löschkandidaten.

## CORE-A09 Dispatch scheitert an benannten Callbacks

- **Ort:** [Core.Tasks.LocalTask.cppm](../../src/core/Core.Tasks.LocalTask.cppm),
  `Model<T>::Model(T&&)` Zeile 25 und Weiterleitung Zeilen 44–54.
- **Compilerreproduktion:** `auto callback = [] {}; Scheduler::Dispatch(callback);`
  erzeugt `no matching function for call to construct_at`. Der äußere
  Forwarding-Konstruktor entfernt den Referenztyp und versucht anschließend,
  das Lvalue an einen reinen Rvalue-Konstruktor zu binden.
- **Reichweite:** vorhandene produktive Aufrufer reichen temporäre Lambdas
  weiter; kein aktuell defekter Produktionsaufruf gefunden.
- **Reparatur:** Payload-Konstruktion muss kopierbare Lvalues und bewegliche
  Rvalues unterstützen. Größen-/Ausrichtungsprüfungen und Move-only-Unterstützung
  erhalten.
- **Regression:** benannte Lambda dispatchen und einmalige Ausführung prüfen.
- **Ohne Fix:** eine gewöhnliche, von der API nahegelegte Aufrufform kompiliert nicht.

## CORE-A10 StringID vergleicht im Debug-Build unterschiedliche Identitäten

- **Ort:** [Core.Hash.cppm](../../src/core/Core.Hash.cppm), Zeilen 76–79.
- **Mechanismus:** defaultiertes `<=>` vergleicht auch `DebugString`; `==`
  vergleicht ausschließlich `Value`.
- **Reproduktion:** zwei getrennte `char[]` mit Inhalt `same` liefern gleiche
  IDs, aber keine äquivalente Ordnung. Die Assertion auf `(x <=> y) == 0` schlägt
  im aktuellen Debug-Preset fehl.
- **Reichweite:** keine produktiven geordneten StringID-Container gefunden;
  Graphcode verwendet überwiegend `.Value`. Die öffentliche API bleibt inkonsistent.
- **Reparatur:** `<=>` ebenfalls ausschließlich auf `Value` beziehen.
- **Regression:** getrennte gleichlautende Speicherbereiche statt möglicherweise
  zusammengelegter Stringliterale; Debug- und Release-Vertrag müssen übereinstimmen.
- **Ohne Fix:** geordnete Container können gleiche IDs im Debug-Build getrennt halten.

## CORE-A11 CallbackRegistry-Test hat ein eigenes Datenrennen

- **Ort:** [Test.Core.CallbackRegistry.cpp](../../tests/unit/core/Test.Core.CallbackRegistry.cpp),
  Leser Zeile 209 und Schreiber Zeile 220.
- **Mechanismus:** der Reader kopiert das gemeinsam referenzierte nichtatomare
  `tk`, während der andere Thread es neu zuweist.
- **Reparatur:** Reader hält den ursprünglichen Token als Wert; der Writer
  verwaltet seine Ersatz-Tokens separat. Ersatz-Callbacks liefern einen anderen
  Sentinel, damit eine falsche Ausführung durch einen alten Token erkennbar wird.
- **Bewertung:** Befund betrifft den Test, nicht automatisch die Registry.
  Deren Snapshot unter Lock und Ausführung außerhalb des Locks ist sinnvoll.
- **Ohne Fix:** die Prüfung kann selbst undefiniertes Verhalten erzeugen und
  belegt die beabsichtigte Nebenläufigkeitsgarantie nicht.

## CORE-A12 Telemetriepuffer ohne Leser und unvollständige Einbindung

- **Orte:** [Core.Telemetry.cppm](../../src/core/Core.Telemetry.cppm),
  `m_SampleBuffer` Zeilen 254–255; Schreibstelle in
  [Core.Telemetry.cpp](../../src/core/Core.Telemetry.cpp), Zeilen 156–164.
- **Evidenz:** Gesamtsuche fand nur Deklaration und Schreibstelle; keinen
  Accessor, Friend-Zugriff, Serialisierer oder Private-Zugriffstrick.
  `sizeof(TimingSample) == 24` im verwendeten Build, daher
  `4096 * 120 * 24 == 11,796,480` Byte, also **11,25 MiB** eingebetteter Speicher.
  Dies ist keine Messung von RSS- oder Laufzeitgewinnen.
- **Reparatur:** ungenutzten Puffer und Schreibpfad entfernen, SampleCount und
  die separat genutzten Aggregate/Passzeiten erhalten. Der kleinste Schnitt
  entfernt ungefähr zehn Zeilen; kein neuer Trace-Export ist erforderlich.
- **Einbindungslücke:** unter `src` keine Aufrufer von Telemetrie-BeginFrame,
  EndFrame, SetTaskStats oder den betreffenden Historie-/Kategorieabfragen
  gefunden; dafür gibt es Testaufrufer. Renderer-Passzeiten und ScopedTimer
  existieren unabhängig davon. Nicht das komplette Telemetry-Modul als tot erklären.
- **Ohne Bereinigung:** reservierter Speicher und Schreibarbeit ohne Verbraucher;
  ein Teil der angebotenen Beobachtungsoberfläche bleibt unintegriert.

## CORE-A13 Diagnosefelder haben falsche Semantik

### CORE-A13a Geschätzte Kosten heißen Nanosekunden

[Core.Dag.TaskGraph.cpp](../../src/core/Core.Dag.TaskGraph.cpp), Zeilen 1215 und
1551: `LastCriticalPathTimeNs()` gibt `ScheduleStats::criticalPathCost` zurück,
also die Summe von `EstimatedCost`, keine gemessene Zeit. Ein Standardpass kann
damit unabhängig von seiner Laufzeit den Wert 1 liefern.

**Reparatur:** überflüssige Zeit-API entfernen oder als geschätzte Kosten
benennen. `GetScheduleStats().criticalPathCost` existiert bereits. Keine neue
Zeitmessung erforderlich. Es wurden keine produktiven Leser gefunden; der
FrameGraph reicht den Wert weiter.

### CORE-A13b Schedulerwerte verschwinden bei der Telemetrieübergabe

[Core.Telemetry.cpp](../../src/core/Core.Telemetry.cpp), `SetTaskStats`,
Zeilen 202–216: `InFlightTasks` und `QueuedTasks` werden nicht übernommen und
auch von `EndFrame()` nicht gesetzt. Ein Snapshot mit 7 und 3 kann so weiterhin
zweimal 0 darstellen.

**Reparatur:** beide Werte konsistent übertragen oder die ungenutzten
Duplikatfelder bei einer bewussten API-Bereinigung entfernen. Der vorhandene
Exporttest in [Test.CoreTasks.cpp](../../tests/unit/core/Test.CoreTasks.cpp),
`CoreTasks.SchedulerStatsCanBeExportedToFrameTelemetry` ab Zeile 1015, nimmt
den Snapshot nach WaitForAll und prüft diese Felder nicht.
Ein Test mit explizit nichtnull Werten fehlt. Nicht dynamisch reproduziert.

## Organisation und durchgehender Stil

### Beibehalten

- Flache, gut suchbare Modulnamen; `Tasks` ist bereits nach Lebensdauer,
  Dispatch, Worker, Tokens und Statistiken aufgeteilt.
- Unterschiedliche Heap-Typen: IndexedHeap dient Dijkstra mit DecreaseKey,
  BoundedHeap begrenzter Auswahl bei Nachbarschaftssuchen. Zusammenlegen
  würde unterschiedliche Verträge vermischen.
- `IIOBackend`: reale Backend-/Testgrenze mit mehreren Implementierungen.
- [Core.GeometryFormatCatalog.inc](../../src/core/Core.GeometryFormatCatalog.inc):
  tatsächlich von Geometry und Assets geteilte Tabelle. Verschieben in einen
  dieser beiden Consumer würde die gemeinsame Ownership erschweren.
- Explizite Configvalidierung und Preview/Apply-Verträge. Die 1.447 Zeilen
  `EngineLoad.cpp` rechtfertigen allein keinen generischen Serializer.
- Konkrete Absicherungen für TaskGraph-Completion-Lebensdauer, Thread-Affinität,
  Replay-Invalidierung, Schedulerinstanzen und Worker-Wakeups.

### Gezielt ändern

- CORE-A07 zeigt echte Verhaltensdrift zwischen zwei Graph-Compilern. Ihre
  API-Notwendigkeit und Mechanismen zuerst klären; erst danach gegebenenfalls
  Compile-, Replay- und Execute-Verantwortungen innerhalb des bestehenden
  TaskGraph-Moduls trennen. Keine neue universelle Abstraktion voranstellen.
- CORE-A12 ist ein konkreter Löschkandidat ohne Verbraucher. Dagegen sind
  Coroutine- und Process-Entfernungen durch RUNTIME-325/RUNTIME-282 gebunden.
- Stilmischung: `nodes_/less_` gegenüber `m_Data/m_Arena`, `top()` gegenüber
  `Top()`, einzelne `export`-Deklarationen gegenüber `export namespace`,
  `unsigned` gegenüber `std::uint32_t`, verschiedene Initialisierungsformen.
  Das beschreibt Inkonsistenz, nicht automatisch einen Bug. Beim fachlichen
  Eingriff lokale Einheitlichkeit herstellen; keine globale Umbenennungswelle.
- [Core.Base64.cppm](../../src/core/Core.Base64.cppm) enthält nichttriviale
  nicht-template Encode/Decode-Implementierungen. Gemäß Modulvertrag gehören
  diese in eine private `.cpp`; kleine constexpr-Helfer können inline bleiben.
- Dokumentationsprüfung: 59 Dateien, davon 38 Interfaces, 58 C++-Dateien und
  ein README; **25 fehlende Synopsen**, **82 heuristische Review-Hinweise**.
  Die 82 Hinweise sind keine 82 bestätigten Vertragsverletzungen.
- [Core-README](../../src/core/README.md) hat 330 Zeilen und dupliziert viele
  Verträge. Die ReferenceScene-Beschreibung ab Zeile 288 nennt noch
  `Engine::Initialize()` als Bootstrap-Owner; tatsächlich liegt die Komposition
  in [Sandbox.cpp](../../src/app/Sandbox/Sandbox.cpp), ab Zeile 79.
  README auf einen aktuellen Einstieg mit Links kürzen; historische Narrative
  und doppelte API-Erklärungen nicht an mehreren Stellen pflegen.

## Weitere offene Vertragsfragen und bewusste Nichtbefunde

Diese Punkte waren Nebenbefunde. Sie haben nicht dieselbe Evidenzstärke wie die
reproduzierten CORE-Fehler und dürfen nicht ungeprüft als bestätigte Bugs in eine
Gesamtliste übernommen werden.

| Thema | Beobachtung und nächster Entscheid |
|---|---|
| Arena-Marker | `Reset()` erhöht die Epoch nicht; Marker enthalten keine Arenaidentität. Alte oder fremde Marker mit passenden Werten können akzeptiert werden. Nur Testaufrufer gefunden; Gültigkeitsvertrag vor Ausbau klären. |
| ArenaAllocator-Erschöpfung | Der Standardallocator-Adapter bricht bei OOM ab, während LinearArena Expected zurückgibt. EPA akzeptiert Caller-Scratch. Erwartete Fehlerpolitik und kleinen Scratch-Fall prüfen; nicht automatisch eine neue Abstraktion verlangen. |
| Pool-Zeiger | Get/GetIfValid geben Pointer zurück, deren Lebensdauer den Lookup-Lock übersteigt. Dokumentation braucht eine klare Borrow-/Reclamation-Grenze. Kein Lock in Get kann allein spätere Pointerverwendung schützen. |
| Pool-ForEach | Reentranzwarnung nennt Add/Remove, aber auch erneute Read-Locks aus demselben Callback sind relevant. Vertrag konkretisieren. |
| Portabilität | Process verwendet ungeschützt POSIX/Linux-Funktionen; PathResolver hat nicht verifizierte Windows/macOS-Zweige. Aktuelle Linux-CI ist davon zu unterscheiden; keine plattformübergreifende Buildbarkeit behaupten. |
| CeilDiv | Verhalten nahe UINT32_MAX und bei Nullteiler ist ausdrücklich dokumentiert/getestet. Kein neu entdeckter stiller Bug; Einschränkung bei neuen Aufrufern beachten. |
| TaskGraph-Completion | Owner-only-Arbeit wird beim Verlust des letzten Completion-Handles absichtlich nicht auf andere Threads verschoben. Fail-closed-Verhalten ist dokumentiert. |
| Scheduler-Lifecycle | Initialize/Shutdown verlangen externe Synchronisierung; nicht jedes hypothetische konkurrierende Lifecycle-Szenario verletzt einen zugesicherten Vertrag. |

## Verifikation des Audits

Die folgenden Prüfungen liefen im ursprünglichen Audit derselben Sitzung am
oben genannten Stand. Die spätere Ablage dieses Berichts fügt keine neue
Engine-Verifikation hinzu.

```bash
cmake --build --preset ci --target IntrinsicCoreTests IntrinsicCoreWrapperUnitTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -L core \
  -R '^(Core|LogRingBuffer\.|Profiling_|StrongHandle\.|TelemetryPresentTiming\.)'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/skills/intrinsicengine-source-documentation/scripts/audit_source_documentation.py \
  --root . --path src/core --summary --no-fail
```

- **Build:** erfolgreich; vorhandener `ci`-Preset-Baum mit Clang 23 und passendem
  `clang-scan-deps-23`. Kein neuer Vollbuild und keine neue Provisionierung.
- **Core-Tests:** 294/294 bestanden. Der erste, breitere Regex-Lauf enthielt
  zusätzlich 21 Runtime-Tests (17 mit Label `integration`, vier mit Label
  `contract`); diese sind nicht Teil der hier beanspruchten
  frisch gebauten Core-Testmenge.
- **Layering:** 960 Dateien, 10.266 Import-/Include-Referenzen, 89 CMake-Links,
  keine Verletzungen und keine Allowlist-Ausnahmen.
- **Probes:** sechs Laufzeit-Assertions reproduzieren CORE-A01, 04, 05, 06, 07,
  10; eine Compilerprobe reproduziert CORE-A09. Die Assertions beschreiben das
  korrekte Verhalten und schlagen auf dem unveränderten Code fehl. Noch keine
  Fix-/Mutation-Verifikation und keine registrierten neuen Regressionstests.
- **Nicht ausgeführt:** TSan, ASan/UBSan, vollständiger CPU-Gate, GPU-/Sandbox-Lauf,
  Windows-/macOS-Build. Die Coroutine-Leak- und Statistikfeld-Probes wurden nur
  als nächste Regression beschrieben, nicht ausgeführt.

### Lokale temporäre Evidence

Die Original-Probes, Build-/CTest-/Layering-Logs und Beobachtungen liegen lokal in
`tasks/evidence/CORE-AUDIT-20261016/`. Das Verzeichnis ist absichtlich ignoriert;
es darf nicht force-added werden. Es enthält keine benötigten Binärdateien.
Es bleibt während der nachfolgenden Audit-Triage ein aktiver Consumer-Beleg.
Nach Übernahme der Befunde in gepflegte Tests/Tasks und Abschluss der Triage
können diese temporären Dateien gemäß Repository-Retention entfernt werden.

Der Bericht bleibt ohne dieses Verzeichnis nutzbar: Mechanismen, Symbolanker,
beobachtete Ergebnisse und die wesentlichen Regressionrezepte sind hier
enthalten. Absolute `/tmp`-Pfade sind keine dauerhaften Voraussetzungen.

### Kompakte Reproduktionsrezepte

Die vollständigen tatsächlich kompilierten Programme heißen lokal `probe.cpp`,
`probe2.cpp` und `lvalue.cpp`. Die folgenden Auszüge reichen zum Überführen in
Tests der jeweiligen öffentlichen Module; Standardheader, Imports und
`using namespace Extrinsic;` sind im Zieltest zu ergänzen.

```cpp
// CORE-A01; import Extrinsic.Core.IOBackend
Core::IO::FileIOBackend io;
const std::array<std::byte, 1> bytes{std::byte{1}};
assert(!io.Write({.Path = "/dev/full"}, bytes)); // aktuell Erfolg

// CORE-A05; import Extrinsic.Core.ResourcePool und Extrinsic.Core.StrongHandle
struct AuditTag {};
using H = Core::StrongHandle<AuditTag>;
Core::ResourcePool<int, H, 0> pool;
const auto old = pool.Add(1);
pool.Clear();
const auto fresh = pool.Add(2);
assert(old != fresh && !pool.Get(old)); // aktuell gleicher, gültiger Handle

// CORE-A06; import Extrinsic.Core.Process
const auto process = Core::Process::Run({
    .Executable = "sh",
    .Args = {"-c", "exec 1>&- 2>&-; sleep 1"},
    .Timeout = std::chrono::milliseconds(50),
});
assert(process.TimedOut); // aktuell false, ExitCode 0

// CORE-A07; import Extrinsic.Core.Dag.Scheduler
auto graph = Core::Dag::CreateTaskPlanGraph();
const Core::Dag::TaskId self{1u, 1u};
const Core::Dag::TaskId deps[]{self};
assert(graph->Submit({.id = self, .debugName = "Self", .dependsOn = deps}));
assert(!graph->BuildPlan()); // aktuell erfolgreicher Ein-Task-Plan

// CORE-A10; import Extrinsic.Core.Hash; Debug-Build ohne NDEBUG
char a[] = "same", b[] = "same";
const Core::Hash::StringID x(a), y(b);
assert(x == y);
assert((x <=> y) == 0); // aktuell nicht äquivalent
```

CORE-A04 verwendet in `probe2.cpp` eine 1-MiB-Arena: `AllocBytes(0,64)` liefert
die Basis; anschließend wird eine Zweierpotenz zwischen 128 und Kapazität
gewählt, für die `base % alignment != 0`. `AllocBytes(1,alignment)` muss eine
passend ausgerichtete Adresse liefern. Im tatsächlichen Lauf war schon 128
der Gegenfall mit Rest 64. Falls eine andere Allokation für alle getesteten
Ausrichtungen passend liegt, liefert die Probe **kein Urteil** (Exit 77),
statt einen Erfolg für den fehlerhaften Mechanismus zu behaupten.

```cpp
// CORE-A09: eigenständige Compile-Probe, erwarteter korrekter Ausgang: kompiliert.
import Extrinsic.Core.Tasks;
int main() {
    auto callback = [] {};
    Extrinsic::Core::Tasks::Scheduler::Dispatch(callback);
}
```

Die Probes wurden gegen die frisch gebauten `ci`-Module und
`build/ci/lib/libExtrinsicCore.a` kompiliert, mit `-std=c++23 -g -fno-exceptions
-fno-rtti` und den BMI-Zuordnungen aus dem aktuellen `CXXModules.json`.
Diese Befehlsangaben stammen aus den Tool-Aufrufen des Koordinators; ein
vollständiger ursprünglicher Befehlsmitschnitt wurde nicht archiviert. Die
Build-Logs enthalten die Compiler-Ausgabe, die bei erfolgreichem Kompilieren
leer sein kann. Beim Linken wurde `-lpthread` verwendet. Ein anfänglicher Versuch mit `-pthread`
war ein BMI-Konfigurationskonflikt, kein Enginefehler. Jeder Laufzeit-Child war
von außen auf fünf Sekunden begrenzt.

## Anschluss für die spätere Gesamtauswertung

1. Beim [Sammelindex](src-audits.md) beginnen. Ergänzend sowohl
   `docs/reviews/*-src-*-audit.md` als auch `docs/reports/*-src-*-audit.md`
   durchsuchen, damit parallel erstellte Berichte in beiden Ablagen gefunden werden.
   Dieses Namensmuster ist eine Konvention für die anderen Agenten, kein neuer
   Validator oder verpflichtender Repositoryvertrag.
2. Die Befunde nach **Mechanismus und verantwortlichem Modul** zusammenführen.
   Wenn Assets oder Runtime den Speicherfehler ebenfalls melden, ist CORE-A01
   derselbe unterliegende Befund, kein zusätzlicher unabhängiger Bug.
3. Zuerst CORE-A01 bis CORE-A03 behandeln; CORE-A04 bis CORE-A11 danach als kleine
   regressionsfähige Blöcke planen. Jeder Implementierungsplan braucht aktuelle
   Aufruferprüfung, unabhängigen Review und die vorhandenen Repositorygates.
4. CORE-A02/12/13b gemeinsam triagieren, aber Race-Reparatur und Entfernung
   ungenutzter Flächen unterscheidbar halten. CORE-A07/13a gehören zur
   Graph-API-/Compilerentscheidung. CORE-A08 bleibt an RUNTIME-325 gebunden.
5. Für bekannte Typen/Templates und nichttriviale Helfer vor Änderungen die
   Reuse-/Right-Sizing-Verfahren anwenden; keine zusätzliche generische Registry,
   Task-Fassade oder neue Fehlerabstraktion allein aus diesem Audit ableiten.

| Gemeinsame Grenze | Primärer Owner | Mit anderem Audit abgleichen |
|---|---|---|
| Schreiben und gespeicherter Szenenstatus | Core IO + Runtime Scene-Persistenz | Runtime, Assets, App |
| Telemetrie-Kategorien und parallele Timer | Core Telemetry | Graphics Renderer, Vulkan |
| Pool-Löschung und Handles | Core ResourcePool | RHI/Null, Vulkan, Graphics Assets |
| Arena-Ausrichtung und Erschöpfung | Core Memory | Geometry EPA und weitere Scratch-Nutzer |
| Graphen und Scheduler | Core Dag/Tasks | Runtime JobService, RenderPrep, Graphics Executor |
| Formatkatalog | gemeinsamer Core-Datenbesitz | Geometry, Assets |

### Separater CI-Kontext

Am Bezugsstand war `ci-docs` grün. Der Main-Run
[38869189381](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38869189381)
scheiterte vor der CPU-Testausführung: `RenderWorldPoolEngineWiring.SourceRules`
ließ sich keinem registrierten Producer zuordnen. Das ist in
[BUG-244](../../tasks/done/BUG-244-cpu-selection-rejects-source-rules-producer.md)
dokumentiert; der fehlende spätere `test.json`-Report ist ein Folgefehler.

Der Nachtlauf
[38854737500](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38854737500)
scheiterte beim Versiegeln zweier Benchmarkresultate wegen der Backendnamen
`cpu_reference_sparse_cholesky` und `cpu_admm_sparse_cholesky`. Owner ist
[BUG-245](../../tasks/done/BUG-245-nightly-smoke-backend-manifest-mismatch.md).
Diese beiden bereits parallel erfassten CI-Probleme sind keine Ergebnisse einer
Core-Reparatur und werden hier nicht erneut als Core-Tasks angelegt.

Der Bericht, der Sammelindex und die beiden BUG-Notizen waren bei der Ablage
noch uncommittete Ergebnisse paralleler Audits. Bei einem späteren Commit die
verlinkten neuen Dokumente gemeinsam oder in Abhängigkeitsreihenfolge übernehmen;
eine grüne Linkprüfung des Arbeitsbaums allein beweist keine vollständige
Commit-Auswahl. Dieser Bericht nimmt keine fremden Änderungen in einen Commit auf.
