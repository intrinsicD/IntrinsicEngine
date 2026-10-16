# Assets Audit vom 16. Oktober 2026

## Fragen und Auftrag

Die ursprünglichen Fragen des Nutzers, unverändert:

1. „sollten wir assets aufräumen?“
2. „anders organisieren?“
3. „hat die implementierung einen durchgenden stil?“
4. „gibt es offene punkte, bugs, implementierungs gaps?“

Zusätzlich beauftragt: „lasse subagenten aus verschiedenen \"use-case rollen\" und
perspektiven darauf schauen.“ Dieser Bericht hält die Ergebnisse für die
anschließende Auswertung durch Codex 6 Astra Ultra und den Vergleich mit weiteren
`src`-Berichten fest. Einstieg in die Serie: [Berichte zu src](src-audits.md).

Die Prüfung dient dem bestehenden Framework24-Konvergenzziel aus
[AGENTS.md](../../AGENTS.md) und dem
[Produktvertrag](../product/framework24-convergence.md): zuverlässige Import-,
Reload- und Asset-Lebenszyklen unterstützen diese Workflows unmittelbar. Sie
begründet keine neue Feature-Priorität und genehmigt noch keinen Umbau.

## Ergebnis und Gültigkeit

**Gezielt aufräumen; die flache Verzeichnisstruktur behalten.** Die wichtigsten
Arbeiten sind Lifecycle-Verträge, doppelte Reload-Logik, überflüssige
GPU-Zustandsverwaltung und die Lebensdauer gespeicherter Loader. Zusätzliche
Unterordner oder eine weitere Service-/Import-Abstraktion lösen diese Probleme
nicht.

| Eigenschaft | Stand |
| --- | --- |
| Prüfdatum | 2026-10-16, Europe/Berlin |
| Geprüfter Quellstand | `66da33fd6bfabb20408607b8361354fc9adb420d` auf `main` |
| Quelländerungen durch das Audit | Keine |
| Größe | 11 Modulinterfaces, 9 Implementierungsdateien, 3.497 C++-Quellzeilen; 3.686 Zeilen einschließlich README und CMake |
| Angenommene Nutzung | Ein lokaler Editor; Hintergrunddekodierung, danach überwiegend Veröffentlichung und Ereigniszustellung auf dem Hauptthread |
| Vorhandene Tests | Zwei Testprogramme gebaut, 133 ausgewählte CTests bestanden |
| Zusätzliche Reproduktionen | Drei temporäre Tests verletzen ihre Soll-Assertions und belegen F01–F03 |
| Bereits behobene Befunde | Keine durch diese Prüfung |

Zeilennummern unten beziehen sich auf diesen Commit. Bei einem späteren Stand
zuerst den genannten Symbolnamen suchen und Änderungen seit diesem Commit lesen.
Ein grüner bestehender Testlauf widerlegt die drei zusätzlich reproduzierten
Lücken nicht. Die neuen Reproduktionen sind noch keine eingecheckten
Regressionstests und enthalten keine Fixes oder Mutationstests eines Fixes.

## Perspektiven und Abdeckung

| Rolle | Agent | Untersuchte Fragen und Pfade |
| --- | --- | --- |
| Import- und Editor-Nutzer | `/root/asset_import_user` | Falsche Eingaben, Import-/Reimport-Semantik, Modell-/Texturvalidierung, Runtime-Materialisierung; Fragen 1, 2, 4 |
| Laufender Betrieb und Lebenszyklen | `/root/asset_lifecycle` | Load, Reload, Destroy, Ereignisse, Generationen, Parallelität, Payload-Lebensdauer; Fragen 1, 3, 4 |
| Wartung und neue API-Aufrufer | `/root/asset_maintainer` | Zuständigkeiten, tatsächliche Aufrufer, ungenutzte Oberflächen, Duplikate, Dokumentation und bestehende Aufgaben; Fragen 1–4 |
| Koordination und Gegenprüfung | Hauptagent | Abgleich der Befunde mit Aufrufern und Tests, fokussierter Build, drei deterministische Reproduktionen |

Die drei Subagenten arbeiteten lesend ohne weitere Delegation. Verwendet wurden
Ponytail und Ponytail Audit sowie die einschlägigen IntrinsicEngine-Regeln.
Alle Assets-Module wurden abgedeckt; verbundene Runtime-Pfade wurden gezielt
verfolgt. Dies ist kein vollständiges Audit aller Decoder, der Runtime oder der
Grafikschicht und keine wissenschaftliche Leistungs- oder Operational-Behauptung.

## Zuständigkeiten und Organisation

Der Build ist in [src/assets/CMakeLists.txt](../../src/assets/CMakeLists.txt)
definiert. `ExtrinsicAssets` hängt von `ExtrinsicCore` ab. Die gelesenen Imports
und CMake-Verknüpfungen halten die CPU-only-Grenze ein; in dieser Sitzung wurde
dafür kein separater vollständiger Layering-Validatorlauf ausgeführt.

| Bestehender Teil | Aufgabe und Urteil |
| --- | --- |
| `Asset.Service` | Koordiniert echte Invarianten zwischen Identität, Pfad, Payload, Zuständen und Ereignissen; behalten |
| `Asset.Registry` | Generationale Identität und Metadaten; behalten |
| `Asset.PathIndex` | Eindeutige Pfadzuordnung; behalten |
| `Asset.PayloadStore` / `Asset.TypePool` | CPU-Datenbesitz und Typprüfung ohne RTTI; behalten |
| `Asset.EventBus` | Entkoppelt Veröffentlichung von Hauptthread-Callbacks; behalten und F01 korrigieren |
| `Asset.LoadPipeline` | CPU-Abschluss, Abbruch und Scheduler-Lebensdauer bleiben erforderlich; unbenutzte GPU-Zweige entfernen |
| `Asset.ImportRouter` | Gemeinsame Formateigenschaften und Import-Routing; behalten |
| `Asset.ImportValidation` | Gemeinsame Diagnose zwischen Decode und Materialisierung; behalten |
| `Asset.GeometryPayload` | Typauslöschung vermeidet eine verbotene assets→geometry-Abhängigkeit; behalten |
| `Asset.ModelTexturePayload` | Zusammenhängende Modell-, Material- und Bilddaten; derzeit kein belegter Nutzen einer Aufteilung |

Decoder sowie ECS-/GPU-Verknüpfung bleiben in Runtime. GPU-Daten gehören weiterhin
`GpuAssetCache`. Die Empfehlung ist kein Auftrag, Schichten oder Modulnamen zu ändern.

## Befundübersicht

Kennungen `ASSETS-Fxx` sind lokale Berichtsschlüssel, keine neu angelegten Tasks.
P2 bedeutet einen konkreten Fehler oder eine relevante Bereinigung, P3 eine
nachrangige Diagnose-/Wartungslücke. Die Reihenfolge innerhalb P2 ist eine
Arbeitsreihenfolge, keine Aussage über beobachtete Produktionshäufigkeit.

| Kennung | Priorität | Befund | Evidenz und Reichweite |
| --- | --- | --- | --- |
| ASSETS-F01 | P2 | `Ready` nach Payload-Retirement bei Destroy im Callback | Ausgeführt; öffentlicher Callback-Vertrag, kein nachgewiesener normaler Editor-Crash |
| ASSETS-F02 | P2 | Gleichzeitige Loads verschiedener Typen teilen erfolgreiche ID | Ausgeführt; aktuelle Runtime veröffentlicht überwiegend auf dem Hauptthread |
| ASSETS-F03 | P2 | NaN/Infinity in Materialfaktoren passieren Validierung | Ausgeführt; keine Pixelwirkung behauptet |
| ASSETS-F04 | P2 | Gespeicherte Loader halten ursprüngliche dekodierte Daten zusätzlich fest | Quellfluss und reale Runtime-Aufrufer; keine RAM-Messung |
| ASSETS-F05 | P2 | Unbenutzter GPU-/Fence-Pfad und Request-Felder | Verwendungssuche; Produktionsaufrufer fehlen am geprüften Commit |
| ASSETS-F06 | P2 | Reload-Transaktion doppelt implementiert | Beide Implementierungen und Tests gelesen |
| ASSETS-F07 | P2 | Öffentliches `LoadErased` umgeht typisierte Veröffentlichung | Quellbestätigte API-Lücke; kein Produktionsaufrufer außerhalb des Templates |
| ASSETS-F08 | P3 | Wiederholte Abschlüsse löschen jüngste Diagnosehistorie | Quellbefund; ausschließlich Testkonsumenten, nicht zusätzlich ausgeführt |

### ASSETS-F01 Ready nach Destroy während der Ereigniszustellung

**Fundstellen:** [Asset.EventBus.cpp](../../src/assets/Asset.EventBus.cpp),
`Flush`, Zeilen 54–66 und 100–113;
[Asset.Service.cpp](../../src/assets/Asset.Service.cpp), `Destroy`, Zeilen 342–376.

**Auslöser:** `Load → Tick → Reload → Tick`; der Listener ruft bei `Reloaded`
`Destroy(id)` auf. `Flush` hat den gesamten Stapel bereits lokal entnommen.
`Destroy` kann mit `Flush(id)` den dort noch wartenden `Ready`-Eintrag nicht
erreichen. Nach Retirement wird dieser trotzdem zugestellt, und `Read` schlägt
im `Ready`-Callback fehl. Das widerspricht dem Destroy-Vertrag im Assets-README.

**Reproduktion:** `DestroyDuringReloadedNeverDeliversRetiredReady` prüft
`EXPECT_FALSE(staleReady)`; tatsächlich ist `staleReady == true`.
Bestehende Tests decken gewöhnliches Destroy-Flushing und reentrantes
Subscribe/Publish ab, nicht diese Lifecycle-Mutation.

**Kleinste Korrektur:** Die Service-Zustellung muss vor jedem Verbraucher-Callback
die Gültigkeit nichtterminaler Ereignisse prüfen, oder Destroy muss bis zum Ende
der laufenden Zustellung zurückgestellt werden. Eine Prüfung nur der noch
global wartenden Ereignisse reicht nicht; auch weitere Listener desselben
bereits aktiven `Ready` müssen berücksichtigt werden. Die konkrete Variante ist
mit dem gewünschten Callback-Vertrag abzugleichen, nicht als fertig entworfener
Fix zu übernehmen.

**Betroffen:** Service/EventBus und `Test.AssetService.cpp`; bestehende Reihenfolge
`Reloaded → Ready` sowie Destroy-Tests erhalten. Die drei untersuchten
Produktionslistener löschen derzeit nicht selbst im Callback. Ohne Korrektur
bleibt der öffentliche Vertrag für solche Aufrufer falsch.

### ASSETS-F02 Typprüfung fehlt im konkurrierenden Pfadtreffer

**Fundstelle:** [Asset.Service.cpp](../../src/assets/Asset.Service.cpp),
`LoadErased`, Zeilen 149–160 gegenüber 179–185.

**Auslöser:** Zwei Threads passieren die erste Pfadsuche vor der Veröffentlichung,
laden aber unterschiedliche Typen. Ein Loader wartet, während der andere seinen
Load abschließt. Der Verlierer des anschließenden `pathIndex.Insert` gibt die
Gewinner-ID zurück, ohne den Typ wie beim normalen Pfadtreffer zu prüfen.

**Reproduktion:** `ConcurrentDifferentTypesRejectTheLosingLoad` hält den
Texture-Loader mit Condition Variable an, führt den Mesh-Load desselben Pfads
vollständig aus und lässt danach den Texture-Loader weiterlaufen. Erwartet:
`TypeMismatch` beim Texture-Load. Tatsächlich meldet auch dieser Erfolg.

**Zusätzliche Quellbeobachtung:** Die ID wird im Pfadindex sichtbar, bevor
Rückwärts-Pfad, Payload-Slot, Reload-Registrierung und Enqueue vollständig sind.
Die einzelne fehlende Typprüfung ist daher nicht die gesamte Frage der
parallelen Veröffentlichbarkeit.

**Kleinste Korrektur:** Typprüfung im Gewinnerpfad angleichen; anschließend
Thread-Affinität der öffentlichen Service-Operationen ausdrücklich festlegen
oder die gesamte Veröffentlichung absichern. Nicht allein aus internen Mutexen
vollständige Service-Thread-Sicherheit ableiten. Falls die API auf den
Hauptthread begrenzt werden soll, ist das eine ausdrückliche Vertragsentscheidung.

**Betroffen:** `LoadErased`, `Test.AssetService.cpp`, API-Dokumentation. Heutige
Runtime-Aufrufer wenden worker-dekodierte Daten überwiegend auf dem Hauptthread
an; ein entsprechender Produktionsfehler wurde nicht gefunden.

### ASSETS-F03 Nicht endliche Materialfaktoren werden akzeptiert

**Fundstelle:** [Asset.ModelTexturePayload.cpp](../../src/assets/Asset.ModelTexturePayload.cpp),
`ValidateMaterial`, Zeilen 24–35. Die Runtime übernimmt Faktoren in
[Runtime.AssetWorkflowModelMaterialization.cpp](../../src/runtime/AssetWorkflow/Runtime.AssetWorkflowModelMaterialization.cpp),
um Zeilen 753–757 und 898–903.

**Auslöser und Reproduktion:** Ein gültiges `MakeModelPayload()` aus dem
vorhandenen Test besteht zunächst die Validierung. Danach werden getrennt
`Materials[0].RoughnessFactor = quiet_NaN()` und
`Materials[0].BaseColorFactor[0] = infinity()` gesetzt. Die Soll-Assertion
`EXPECT_FALSE(ValidateAssetModelScenePayload(payload).has_value())` scheitert
für beide Varianten: der Validator akzeptiert sie.

**Kleinste Korrektur:** Endlichkeit der Materialfaktoren im bestehenden
Validator prüfen. Wertebereichsregeln zusätzlich aus dem konkreten Payload-Vertrag
ableiten; dieser Befund beweist zunächst die fehlende Endlichkeitsprüfung.
`Test.Asset.ModelTexturePayload.cpp` um beide Fälle ergänzen. Referenzen und
nicht endliche Knotentransformationen sind bereits getestet.

**Auswirkung:** Ungültige Zahlen gelangen über eine als erfolgreich gemeldete
Validierungsgrenze. Vollständige Faktorprojektion ist teilweise noch offen;
daraus wird kein nachgewiesener GPU-/Darstellungsfehler abgeleitet.

### ASSETS-F04 Gespeicherte Loader behalten alte Payload-Daten

**Fundstellen:** [Asset.Service.cpp](../../src/assets/Asset.Service.cpp),
Zeilen 208–225; [Runtime.AssetWorkflowImportExecutor.cpp](../../src/runtime/AssetWorkflow/Runtime.AssetWorkflowImportExecutor.cpp),
Modell-Load 665–672, Textur-Load 791–798, Mesh 1166–1186, Graph ab 1260,
PointCloud ab 1333; [Runtime.AssetWorkflowModelMaterialization.cpp](../../src/runtime/AssetWorkflow/Runtime.AssetWorkflowModelMaterialization.cpp),
`LoadEmbeddedTextureAsset`, 126–143; [Runtime.TextureBakeModule.cpp](../../src/runtime/Modules/TextureBake/Runtime.TextureBakeModule.cpp),
2202–2208. Damit sind alle sieben Produktionsaufrufe von `Load<T>` einbezogen.

**Auslöser:** Runtime fängt einen dekodierten Payload als `shared_ptr` im Loader
ein und gibt beim Modell-/Dateitextur-Load `*payload` als Wert zurück. Der Store besitzt die veröffentlichte
Kopie; der dauerhaft registrierte Loader hält den ursprünglichen Payload.
Ein typisierter Reload ersetzt den Store-Inhalt, nicht den ursprünglichen Loader.
Insbesondere Texturbytes und Modellcontainer bleiben dadurch zusätzlich erhalten;
dies bedeutet nicht, dass jeder intern geteilte Geometriepuffer doppelt existiert.

Weitere Aufrufer haben denselben dauerhaften Capture-Besitz mit unterschiedlicher
Größe und Erstlade-Semantik: eingebettete Texturen kopieren `image` samt
`PixelBytes` direkt per Wert; Graph/PointCloud halten ihre Payload-Zeiger. Mesh
überträgt beim ersten Load den vorbereiteten Wert, behält aber `meshPayload`
als Nachlade-Snapshot. TextureBake speichert `metadata` per Wert; dessen
Speichergröße wurde nicht bewertet. Diese Unterschiede gehören in die
Eigentumsprüfung und dürfen nicht pauschal als sieben identische Vollkopien
beschrieben werden.

**Auswirkung:** Reale zusätzliche Speicherbindung bis Destroy, kein bewiesenes
unbegrenztes Leak und keine gemessene Speicherzahl. Parameterloses `Reload(id)`
verwendet dokumentiert den anfänglichen Loader; Produktionsaufrufer dieser
Überladung wurden nicht gefunden, wohl aber Tests.

**Kleinste Richtung:** Für bereits dekodierte Runtime-Daten Veröffentlichung und
dauerhaft aufbewahrte Nachladefunktion trennen, oder die unbenutzte öffentliche
Nachladeoberfläche nach erneuter Aufrufersuche entfernen. Nicht einfach den
Capture leeren und damit die bestehende Reload-Semantik brechen.

**Betroffen:** Service, alle sieben genannten Produktionsaufrufe und deren
Reload-/Reimport-/Bake-Tests. Eine Korrektur beim zentralen Besitzer kann
Einzellösungen an den Importstellen vermeiden.
Ein gezielter Lifetime-Test sollte festhalten, welche Generation nach
Veröffentlichung und Reload noch besessen werden darf. Kein Performanceexperiment
ist nötig, um diese Eigentumsfrage zu testen.

### ASSETS-F05 Ungenutzten GPU-Lebenszyklus und Request-Ballast entfernen

**Fundstellen:** [Asset.LoadPipeline.cpp](../../src/assets/Asset.LoadPipeline.cpp),
GPU-Übergang 213–225, Upload-/Fence-Methoden 261–364;
[Asset.LoadPipeline.cppm](../../src/assets/Asset.LoadPipeline.cppm), `LoadRequest`.

**Verwendungssuche:** `ArmGpuFence`, `CompleteGpuFence`, `OnGpuUploaded` werden
außerhalb der Implementierung nur von Tests benutzt. Alle drei Service-Anfragen
setzen `needsGpuUpload=false`. Tatsächliche GPU-Residency liegt in Runtime und
`GpuAssetCache`. `uploadDone`/`finalized` werden nur geschrieben;
`LoadRequest.typeId/path` werden gespeichert, aber nicht ausgewertet.
Auch `MarkFailed` hat nur Testaufrufer; seine Notwendigkeit gehört in dieselbe
Verwendungssichtung, ohne daraus automatisch fehlende Fehlerbehandlung abzuleiten.

**Kleinste Korrektur:** Unbenutzte GPU-Zweige, Fence-Map, entsprechende Felder und
rein darauf bezogene Tests gemeinsam entfernen. CPU-Abschluss, Abbruch und
Invalidierung geplanter Callbacks sind tragende Mechanismen und bleiben bestehen.
Der Test `PathIsCopiedInRequestAndSurvivesCallerString` prüft nur Zustand und
In-flight-Zähler, nicht den behaupteten Pfadinhalt; für ein entferntes unbenutztes
Feld ist kein neuer Pfadtest nötig.

**Umfang:** Grob 140–180 Produktionszeilen erscheinen allein im GPU-Zweig
entfernbar. Dies ist eine Review-Schätzung, kein implementierter Diff und keine
Laufzeitaussage. Neue Abhängigkeiten sind dafür nicht nötig. Wiederaufnahme erst
bei einem konkreten Verbraucher mit einem entsprechenden Vertrag.

### ASSETS-F06 Eine gemeinsame Reload-Transaktion

**Fundstellen:** [Asset.Service.cppm](../../src/assets/Asset.Service.cppm),
Zeilen 143–231; [Asset.Service.cpp](../../src/assets/Asset.Service.cpp),
Zeilen 249–341.

**Problem:** Checkpoint, Slot-Aktualisierung, Zustandswechsel, Rollback und
Ereignisfolge werden separat implementiert. Änderungen an diesen Invarianten
müssen doppelt korrekt sein; erhebliche nicht typabhängige Logik liegt im
exportierten Template. Die typisierte Variante wird von Runtime-Reimport/Bake
verwendet, die gespeicherte Loader-Variante von Tests.

**Kleinste Korrektur:** Ein privater, nicht templatisierter Besitzer der
Transaktion; die typisierte Hülle adaptiert nur Payload-Erzeugung und
Veröffentlichung. Keine generische Transaktionsbibliothek hinzufügen.

**Verifikation:** Beide Aufrufvarianten, fehlgeschlagene Loader, Rollback vor
Commit sowie Ticketgeneration und Ereignisreihenfolge weiter testen.
Eine Entscheidung über F04 kann die benötigte öffentliche Oberfläche verkleinern;
deshalb die beiden Befunde vor der Umsetzung zusammen betrachten, ohne
Speichersemantik und mechanische Zusammenführung unkontrolliert zu vermischen.

### ASSETS-F07 LoadErased als privaten Helfer behandeln

**Fundstellen:** [Asset.Service.cppm](../../src/assets/Asset.Service.cppm),
öffentliche Deklaration um Zeile 45; [Asset.Service.cpp](../../src/assets/Asset.Service.cpp),
Vertrauen in das Loader-Ticket um Zeilen 172, 195 und 228.

**Konkreter Fall:** Ein direkter Aufrufer gibt `PayloadTicket{1, 1}` zurück,
ohne im Store Daten zu veröffentlichen. Die Funktion kann dennoch den Slot
übernehmen und `Ready` einplanen. Dieser Pfad wurde statisch verfolgt, nicht
zusätzlich ausgeführt.

**Kleinste Korrektur:** `LoadErased` privat machen. Die vollständige Aufrufersuche
am geprüften Commit findet nur das eigene typisierte `Load`-Template. Ohne
Korrektur bleibt eine unbenötigte öffentliche Umgehung der Veröffentlichungsregel.

### ASSETS-F08 Diagnosehistorie verliert die neueste Version

**Fundstelle:** [Asset.LoadPipeline.cpp](../../src/assets/Asset.LoadPipeline.cpp),
`ArchiveTrailUnlocked`, Zeilen 65–80.

**Konkreter Fall:** 257 abgeschlossene Ladezyklen derselben ID überschreiben
jeweils den Map-Eintrag, hängen aber dieselbe ID erneut in die auf 256 Einträge
begrenzte Reihenfolge. Das Entfernen des ältesten Duplikats löscht den gerade
aktualisierten Map-Eintrag. `GetStageTrail` findet dann unmittelbar nach dem
Abschluss keine Historie mehr. Nicht zusätzlich ausgeführt.

**Kleinste Korrektur:** Jede ID nur einmal in der Reihenfolge halten, falls die
Diagnoseoberfläche bleibt. Andernfalls im Zuge F05 den unbenutzten Diagnosepfad
entfernen. `GetStageTrail` hat derzeit nur Testkonsumenten; entsprechend geringe
unmittelbare Nutzerwirkung.

Die Stempel `AssetIO`/`Decode` sind zudem keine Messung des tatsächlichen
Dateilesens oder Dekodierens: `LoadErased` ruft den Loader vor `EnqueueIO` auf.
Sie dürfen nicht ohne Umbau als Import-Laufzeittelemetrie ausgegeben werden.

## Stil und kleine Bereinigungen

Der Stil ist im Kern einheitlich: benannte Module, PascalCase-Operationen,
`Core::Expected`/`Result`, `m_`-Member und überwiegend private Implementierungen.
Registry-Generationen, Modellreferenzen, Eltern-/Kindkonsistenz, Zyklen,
Erreichbarkeit, Transform-Endlichkeit sowie Überlaufprüfungen vor der
Texturgrößenmultiplikation sind sinnvolle vorhandene Sicherungen.

Folgende kleinere Punkte rechtfertigen keine breite Umbenennungsaktion:

- Alte Lifecycle-Felder verwenden `id/typeId/path`, neuere Datenrecords
  `PayloadKind/TypeId/ValidationReport`. Lokal konsistent weiterarbeiten.
- `TypeMismatch` und `AssetTypeMismatch` benötigen bei Vertragsänderungen eine
  klare Bedeutung, nicht lediglich kosmetische Vereinheitlichung.
- `AssetPayloadTypeIdOf` in
  [Asset.GeometryPayload.cppm](../../src/assets/Asset.GeometryPayload.cppm),
  Zeilen 28–38, und `AssetService::TypeIdOf`, Zeilen 102–113, duplizieren die
  Pointer-Token-Faltung. `Asset.TypePool` ist ihr gemeinsamer Import und der
  vorhandene Besitzer der Typkennungen; dort eine gemeinsame Regel prüfen,
  keinen neuen Wrapper bauen.
- `IsAssetModelSceneGeometryKind` in `Asset.ModelTexturePayload.cpp`, Zeilen
  278–283, dupliziert `IsGeometryPayloadKind`; der Validator ruft beide auf.
  Den allgemeinen bestehenden Helfer verwenden.
- `Read`/`ReadSpan` geben geliehene Ansichten zurück. Die öffentlichen
  Deklarationen sollten Lebensdauer bis Reload/Retire und erlaubte Parallelität
  nennen. Ein aktueller Produktions-Use-after-free wurde nicht festgestellt.
- [Assets-README](../../src/assets/README.md) lässt `ImportValidation` in seinen
  manuell gepflegten Modul-/Dateilisten aus. Besser eine knappe
  Zuständigkeitsübersicht statt eines zweiten Dateiinventars.
- [Architekturdokument](../architecture/assets.md), Zeilen 12–13, ordnet
  primären/externen Byte-Transport Assets zu. Tatsächlich liegen `ReadBytes` und
  `ReadExternalResource` in Runtime, in
  [Runtime.AssetWorkflowModelTextureDecode.cpp](../../src/runtime/AssetWorkflow/Runtime.AssetWorkflowModelTextureDecode.cpp),
  Zeilen 80–112. Die Beschreibung an die tatsächliche Zuständigkeit angleichen.

## Bestehende offene Arbeit und bewusste Grenzen

| Thema | Bestehender Besitzer und Einordnung |
| --- | --- |
| Vollständige authored Faktor-/Texturprojektion | [GRAPHICS-105](../../tasks/active/GRAPHICS-105-unified-mesh-shading-and-attribute-source-authority.md), insbesondere um Zeile 335; kein neuer Assets-Task nötig |
| MaterialX-Teilmenge | [ASSETIO-009](../../tasks/backlog/assets/ASSETIO-009-loss-aware-materialx-subset-import.md), abhängig von GRAPHICS-105 |
| Asynchrone Prüfung externer Modelldateien | [ASSETIO-010](../../tasks/backlog/assets/ASSETIO-010-async-model-companion-preflight.md) |
| Tatsächlicher File/Import-Widget-Ablauf | [ASSETIO-011](../../tasks/backlog/assets/ASSETIO-011-semantic-sandbox-file-import-workflow-matrix.md), abhängig von ASSETIO-010 |

`MetallicRoughnessTexture` und `OcclusionTexture` werden dekodiert und validiert,
aber außerhalb dieser Stufen nicht gelesen. Die Materialisierung übernimmt
Albedo/Normal und verwendet konstante Roughness-/Metallic-Werte. Das ist eine
reale Integrationslücke innerhalb der bereits bekannten GRAPHICS-105-Arbeit.

KTX/KTX2 wird absichtlich nur erkannt und deterministisch als nicht unterstützt
abgewiesen. Standalone-Geometrie-Entities sind laut
[Runtime-README](../../src/runtime/README.md) um Zeile 68 unabhängige
Bearbeitungskopien. Reimport aktualisiert nicht automatisch diese bestehenden
Entities. Beide Verhaltensweisen wurden ausdrücklich nicht als neue Bugs gewertet.

## Verifikation und reproduzierbare Übergabe

### Vorhandene Tests

Tatsächlich am Prüfstand ausgeführt, mit `ci`, Clang 23 und passendem
`clang-scan-deps`, Null/headless, ohne aktivierten promoted Vulkan:

```bash
cmake --build --preset ci --target IntrinsicAssetUnitTests IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 \
  -R '^(AssetEventBus|AssetImportRouter|AssetModelTexturePayload|AssetModelScenePayload|AssetTexture2DPayload|AssetPathIndex|AssetPayloadStore|AssetRegistry|AssetService|AssetLoadPipeline)\.'
```

Ergebnis: **133/133 bestanden**, davon 71 Assets-Unit- und 62
Runtime-Integrationstests. Der Build war erfolgreich; vorhandene Runtime-Warnings
wurden dadurch nicht behoben. Kein vollständiger CPU-, ASan-, UBSan- oder GPU-Gate.

Die historische Regex ist oben exakt wiedergegeben. Ihre Alternativen
`AssetModelScenePayload` und `AssetTexture2DPayload` entsprechen keinen
existierenden Suites und trugen keine Tests bei. Von insgesamt 83 Tests in den
sechs Assets-Unit-Dateien wurden 71 ausgewählt; nicht ausgewählt waren
`AssetRegistry_State` (6), `AssetRegistry_Accounting` (2), `AssetRegistry_Debug`
(1) und `PayloadTicket` (3). Die folgende Tabelle zeigt Zielbesitz, keine
vollständige Ausführung aller Tests dieser Dateien.

| Tests beziehungsweise Source | Ausführbares Ziel |
| --- | --- |
| Sechs Dateien unter `tests/unit/assets` | `IntrinsicAssetUnitTests` |
| `Test.AssetService.cpp`, `Test.AssetLoadPipeline.cpp` | `IntrinsicRuntimeIntegrationTests` |
| Workflow-/Import-Vertragstests unter `tests/contract/runtime` | `IntrinsicRuntimeContractTests`; in diesem Audit nicht neu gebaut/ausgeführt |

Die Zielzuordnung kam aus
`build/ci/test-inventories/RegisteredTestSources.tsv`. Vor Folgeänderungen die
aktuelle Zuordnung erneut prüfen.

### Zusätzliche Reproduktionen

Die Testquellen und vollständigen Logs wurden zunächst unter `/tmp` erzeugt und
für diese Übergabe nach `tasks/evidence/SRC-ASSETS-2026-10-16/` kopiert. Dieses
Verzeichnis ist **ignoriert und nicht Teil eines frischen Git-Checkouts**. Es
enthält keine Fixes und keine dauerhaft registrierten Tests. Der Bericht bleibt
dank der Auslöser und Soll-Assertions oben ohne diesen Ordner verständlich.

| Datei im Evidence-Verzeichnis | Inhalt |
| --- | --- |
| `assets-audit-probe.cpp` / `.log` | F01 und F02; zwei fehlgeschlagene Soll-Assertions |
| `assets-audit-material-probe.cpp` / `.log` | F03; gültige Ausgangsfixture, beide nicht endlichen Varianten werden fälschlich akzeptiert |
| `assets-audit-build.log` / `assets-audit-tests.log` | Erfolgreicher Build und 133 bestandene CTests |
| `assets-audit-ci-cpu.log` / `assets-audit-ci-nightly.log` | Heruntergeladene CI-Joblogs; keine lokal neu ausgeführten Benchmarks |
| `reproduce.txt` | Erneutes Kompilieren/Linken der temporären Tests gegen den vorhandenen `ci`-Build |

Die Probes wurden mit den jeweiligen Compilerargumenten aus
`build/ci/compile_commands.json`, den vorhandenen Modul-Mappings und den frisch
gebauten Assets/Core-Bibliotheken übersetzt. Ihre Ausführung endete mit Exit 1
wegen der genannten Assertions, nicht wegen Buildfehlern. Falls Evidence fehlt,
die Szenarien aus F01–F03 in den jeweiligen vorhandenen Testsuiten nachbauen;
für F03 existiert `MakeModelPayload()` bereits im Zieltest.

Bei einer Umsetzung zuerst den Test ohne Fix rot zeigen, dann den Fix und die
betroffenen Regressionen prüfen. Bestehende Dateien nicht allein für das Audit
umbenennen. Die temporäre Evidence bleibt verfügbar, solange dieser Bericht noch
ein aktiver Verbraucher ist; nach Übernahme der Reproduktionen in dauerhafte
Tests kann sie entsprechend der Repository-Retention entfernt werden.

## CI-Kontext getrennt von den Assets-Befunden

Vorbestehende CI-Fehler und deren Belege stehen ausschließlich in
[BUG-244](../../tasks/backlog/bugs/BUG-244-cpu-selection-rejects-source-rules-producer.md)
(CPU-Testproduzentenzuordnung) und
[BUG-245](../../tasks/backlog/bugs/BUG-245-nightly-smoke-backend-manifest-mismatch.md)
(Benchmark-Versiegelung). Sie wurden nicht im Assets-Audit repariert.

## Einstieg für Codex 6 Astra Ultra

1. [AGENTS.md](../../AGENTS.md), Core-Skill und diesen Bericht lesen; den aktuellen
   Commit und `git status` mit dem geprüften Stand vergleichen. Andere neue
   `src`-Berichte über [den Index](src-audits.md) finden; ergänzend liegen Berichte
   mit dem Dateimuster `*-src-*-audit.md` sowohl in `docs/reviews` als auch
   `docs/reports`.
2. F01–F03 zuerst nachprüfen. Die beschriebenen Auslöser und Assertions dienen
   als konkrete Regressionen; die bestehenden grünen Tests allein genügen nicht.
3. F04/F06 gemeinsam auf den tatsächlich benötigten Reload-Vertrag prüfen.
   Entscheidung über Besitz und öffentliche Oberfläche vor einer Zusammenführung
   treffen. Keine neue Abstraktion nur zum Zusammenführen hinzufügen.
4. F05 separat als Entfernung ungenutzter Zustände/Felder behandeln. Echte
   Scheduler-/Abbruchgarantien erhalten. F08 und rein tote Diagnosetests dabei
   mit bewerten. F07 lässt sich eng als API-Sichtbarkeit korrigieren.
5. README und kleine Helferduplikate nur passend zum berührten Vertrag bereinigen.
   PBR-/MaterialX-/Import-UI-Lücken bei ihren bestehenden Aufgaben belassen.

Diese Schritte beantworten Fragen 1–4; sie sind eine Empfehlung für die
anschließende Planung, keine vorweggenommene Implementierungsfreigabe oder
Abnahme. Bei Umsetzung gelten Plan-Gegenreview, unabhängige Korrektheitsprüfung,
fokussierte Tests und Dokumentationssynchronisierung aus dem Repository-Vertrag.

Nicht geprüft: Live-Sandbox, reale Pixel, komplette Codecimplementierungen,
Sanitizer, vollständige CPU-Suite, Leistung und gemessener Speicherverbrauch.
Keine Aussage dieses Berichts sollte diese Evidenzklassen implizieren.
