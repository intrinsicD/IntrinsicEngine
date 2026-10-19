# Konsolidierter Umsetzungsplan der src Audits

## Auftrag und Ziel

1. „Bitte schaue dir jetzt alle vorhandenen src-Auditberichte an, führe doppelte Befunde zusammen und erstelle einen priorisierten Umsetzungsplan mit klaren Zuständigkeiten. Noch nichts implementieren.“

Zielbezug: Zuverlässige Nutzerabläufe für die [Framework24-Konvergenz](../product/framework24-convergence.md), deren abschließender Entscheid bei [REVIEW-004](../../tasks/backlog/architecture/REVIEW-004-framework24-product-convergence-audit.md) liegt. Der kürzeste Weg aus diesen Berichten ist: konkrete Datenverlust-, Lebenszyklus- und Ergebnisfehler korrigieren, vorhandene Integrationsverträge schließen, danach belegte Duplikate entfernen. Ein neues Framework oder ein großer Verzeichnisumbau ist nicht begründet.

**Status: Umsetzung vom Operator beauftragt.** Der ursprüngliche Plan beantwortet Auftrag 1. Der anschließende Auftrag zur vollständigen Umsetzung, einzeln erteilte Entscheidungen, tatsächliche Zuständigkeiten und Verifikation werden ausschließlich in [HARDEN-090](../../tasks/active/HARDEN-090-src-audit-remediation.md) geführt. Die Tabellen bleiben die konsolidierte Umfangsreferenz; bestehende Task-Verträge bleiben maßgeblich.

## Quellen und Gültigkeit

Ausgewertet wurden sämtliche acht aktuellen Bereichsberichte aus dem [src-Audit-Index](src-audits.md), zusätzlich die beiden älteren, ausdrücklich auf src bezogenen Gap-Analysen. Die Suche umfasste beide Berichtablagen, `docs/reviews` und `docs/reports`.

| Quelle | Stabile Befunde | Verwendung |
| --- | ---: | --- |
| [Core](2026-10-16-src-core-audit.md) | 13 | CORE-A01 bis CORE-A13; A13 enthält zwei Teilaspekte |
| [Assets](2026-10-16-src-assets-audit.md) | 8 | ASSETS-F01 bis ASSETS-F08 |
| [ECS](2026-10-16-src-ecs-audit.md) | 14 | ECS-20261016-001 bis ECS-20261016-014 |
| [Geometry](2026-10-16-src-geometry-audit.md) | 15 | GEO-AUD-01 bis GEO-AUD-15 |
| [Graphics](../reports/2026-10-16-src-graphics-audit.md) | 8 | SRC-GRAPHICS-001 bis SRC-GRAPHICS-008 |
| [Physics](../reports/2026-10-16-src-physics-audit.md) | 15 | PHYS-AUDIT-01 bis PHYS-AUDIT-15 |
| [Platform](../reports/2026-10-16-src-platform-audit.md) | 12 | PLAT01 bis PLAT12 |
| [Runtime](2026-10-16-src-runtime-audit.md) | 14 | SRC-RUNTIME-001 bis SRC-RUNTIME-014 |
| [Geometry, Mai](2026-05-12-src-geometry-gap-analysis.md), [ECS, Mai](2026-05-13-src-ecs-gap-analysis.md) | Historische Themen/Tasks | Mit heutigem Taskstatus abgeglichen; keine zusätzlichen aktuellen Defekte allein aus alten Aussagen abgeleitet |

**99 Originalkennungen ergeben nach Zusammenführung zweier Duplikatgruppen 96 eigenständige nummerierte Befundgruppen: 23 P1, 55 P2 und 18 P3.** Das sind nicht 96 reproduzierte Bugs: enthalten sind Quellbefunde, API-Lücken und Bereinigungsempfehlungen. Bereits bekannte Tasks und unnummerierte Empfehlungen stehen zusätzlich unten; sie erhöhen diese Zahl nicht.

Alle aktuellen Berichte untersuchten `66da33fd6bfabb20408607b8361354fc9adb420d`, auch HEAD dieser Planung. `src`, Tests und Build-Konfiguration waren gegenüber diesem Commit unverändert. Die Berichte, der Index und zwei CI-Bugnotizen lagen bereits als uncommittete Dateien vor. Ihre Ergebnisse werden hier übernommen, nicht als erneut ausgeführte Tests ausgegeben. Ein eigener src/app-Bericht liegt nicht vor; App-Aufrufer sind nur im Umfang der Bereichsberichte erfasst.

Evidenzkürzel unten: **R** = Bericht nennt ausgeführten Repro, **S** = Quell-/Callerbefund, **A** = arithmetische oder extrahierte Probe statt realer Backend-Ausführung, **D** = Dokumentation/Verwendungssuche. Gemischte Zeilen haben entsprechend gemischte Nachweise. Jeder Owner übernimmt vor Umsetzung den konkreten Trigger als Regression am dann aktuellen Stand. Die Originalberichte bleiben die Quelle für Dateianker, Rezepte und genaue Prüfgrenzen.

## Deduplikation und Verantwortungsgrenzen

| Kanonischer Befund | Zusammengeführte Originalkennungen | Federführung und Abgrenzung |
| --- | --- | --- |
| Physics verwendet falschen Koordinatenraum | PHYS-AUDIT-02 = SRC-RUNTIME-005 = ECS-20261016-011 | **Runtime/PhysicsIntegration**. ECS liefert Transformationshilfen, Physics erwartet Weltkoordinaten. Ein Fix mit Hin- und Rücktransformation. P1 aus Physics beibehalten: falsche Simulation unterstützter Hierarchien; ECS/Runtime hatten P2. |
| Physics ignoriert Authoring-Einstellungen | PHYS-AUDIT-03 = ECS-20261016-012 | **Runtime/PhysicsIntegration**. Runtime prüft den bestehenden Teilnahme-/Filtervertrag; nicht unterstützte Einstellungen sichtbar zurückweisen. Paarweise Filterpolitik nicht ohne gesonderten Architekturentscheid nach Physics verschieben. P1 wegen still falscher Kollisionen; keine pauschale Erweiterung auf alle deklarierten Features. |

Folgende ähnliche Befunde bleiben bewusst eigenständig:

- **Save-Kette:** CORE-A01 prüft Schreibabschluss nicht; GEO-AUD-02 trunciert Graphdateien vor Validierung; SRC-RUNTIME-003 speichert die falsche History-Revision; SRC-RUNTIME-004 lässt Bindings im Snapshot aus. Verschiedene Ursachen und Regressionen.
- **Kontaktkette:** GEO-AUD-03 liefert falsche GJK/EPA-Tiefe; PHYS-AUDIT-01 verwendet alte Tiefe in weiteren Solveriterationen. Beide brauchen analytische Sollwerte; Gleichheit mit einer ebenfalls fehlerhaften Referenz reicht nicht.
- **Identität und Datenformen:** CORE-A05 und PHYS-AUDIT-07 betreffen verschiedene Pools; GEO-AUD-09 betrifft Viewindizes. ECS-004 und SRC-RUNTIME-001 betreffen verschiedene Property-Produzenten. GEO-AUD-05 und ECS-005 sind verschiedene Deleted-Slot-/Maskenfehler.
- **Bounds:** ECS-007 erzeugt falsche OBBs; ECS-008 extrahiert deren AABB falsch. Producer und Consumer separat testen.
- **GPU-Abschluss:** SRC-GRAPHICS-004, GRAPHICS-157 und BUG-232 betreffen native Freigabe, Descriptor-Retirement und Commit-Copy. Keine gemeinsame Erledigung allein durch einen der Fixes.
- **Runtime-fremd berichtet, dennoch Runtime-eigen:** SRC-GRAPHICS-008 sowie PLAT05, PLAT09 und PLAT10 kommen im Runtime-Bericht nicht nochmals als eigene ID vor. Sie werden einmal zugeordnet, nicht als weitere Duplikate abgezogen.

## Priorisierte Reihenfolge und Zuständigkeiten

P1 bedeutet zuerst bearbeiten: Daten-/Speicherintegrität, Absturz, falscher operativer Ablauf. P2 folgt für weitere Nutzer- und API-Verträge. P3 ist gezielte Wartung. Dies sind lokale Reihenfolgen innerhalb der beauftragten Auditnacharbeit; das stehende Produktziel P0 wird nicht ersetzt. Die Tabellen übernehmen grundsätzlich die Berichtpriorität. Assets-F05 und F06 werden unten ausdrücklich von P2 auf P3 zurückgestellt: F05 betrifft produktiv ungenutzten Ballast; F06 ist eine echte Duplikation mit produktiven Reload-Aufrufern, aber ohne nachgewiesenen eigenen Verhaltensfehler. F07 bleibt als öffentliche Vertragslücke P2, wie die latenten API-Fälle PHYS-13 und PLAT08.

| Welle | Reihenfolge / Arbeit | Verantwortlichkeit | Abschluss und Abhängigkeiten |
| --- | --- | --- | --- |
| 0, parallel | Bestehende CI-Fehler BUG-244 und BUG-245 | **CI/Tooling**, bei BUG-245 mit den beiden Benchmark-Runner-Ownern | CPU-Auswahl bzw. Backend-/Manifestkonsistenz wiederherstellen, keine Checks abschwächen. Kein neuer Vorab-Gate für lokale, gezielte Fehlerrepros. |
| 1a | Speichern/Export, Property-Abbrüche, Undo, Container-Lifetime und Hierarchiefehler | **Core**, **Geometry**, **ECS**, **Runtime/Scene und Editor** gemäß Einzeltabelle | P1-Regressionen vor Fix rot, nach Fix grün; negative Pfade erhalten alte Datei, Szene oder History. Pro Ursache ein kleiner Fix. |
| 1b, parallel zu 1a | Core-Races, Socket-Timeout, GPU-Upload-/Recordingfehler | **Core**, **Platform**, **Graphics** | Reale Fehlerübergänge absichern. Grafik-G004 beginnt mit erreichbarem Caller-/Lifecycle-Repro; extrahierte Ringprobe wird zum Test der echten Implementierung. |
| 1c | Kontaktgeometrie und Solver; Physics-Bridge, Filter, Wake und Diagnosen | **Geometry → Physics**, **Runtime/PhysicsIntegration** | GEO-03 und PHYS-01 unabhängig korrigieren, danach gemeinsame World-Abnahme. Ein Runtime-Writer für alle Änderungen an PhysicsModule. Filterpolitik bleibt an der bestehenden Runtime-Grenze. |
| 2 | Übrige P2-Zeilen; innerhalb eines Owners zuerst echte Nutzerpfade, danach bislang ungenutzte öffentliche API-Randfälle | Jeweils **fett markierter Owner** unten | Export-/Numerik-/Interaktionsfehler vor API-Randfällen ohne produktive Caller. GEOM-114 und RUNTIME-276 als bestehende Tasks nutzen. Native Nachweise separat einplanen. |
| 3 | P3, belegte Wiederverwendung, tote Felder, gezielte Dokumentation | Jeweils **Subsystem-Owner** | Verhaltenstests vor/nach Refactoring gleich; Löschungen erst nach aktueller Callersuche. Naming/Ordnerumbauten separat und nur nach erforderlicher Entscheidung. |

**Empfohlener erster konkreter Slice:** SRC-RUNTIME-003, federführend Runtime/Scene mit History-Owner. Snapshotrevision mitführen, späteren Edit dirty lassen; keine gleichzeitige History-Neugestaltung. Danach SRC-RUNTIME-001 und CORE-A01 priorisieren. Parallel sind Platform/Socket und Graphics/Upload sinnvoll, sofern eigene Worktrees und Buildverzeichnisse vorhanden sind. „Parallel“ bedeutet keine mehreren Writer im gemeinsamen Checkout.

Die Federführung liefert Fix, betroffene Caller/Fixtures/Config, Regression und fachliche Dokumentation; beteiligte Owner prüfen ihren Integrationsvertrag. Eine unabhängige, nicht schreibende Reviewrolle beurteilt die Abnahme am exakten Diff. Eine Projektperson oder ein Agent ist noch keinem Umsetzungspaket zugeteilt.

## Vollständige Zuordnung der nummerierten Befunde

Die Tabellen sind die vollständige Arbeitsliste, keine zusätzlichen Task-IDs. Wenn kein bestehender Task ausdrücklich genannt wird, ist bislang kein passender Fix-Task identifiziert; bei späterer Beauftragung entsteht höchstens ein kleiner Task je unabhängiger Ursache. Eine gemeinsame Tabellenzeile oder Welle ist kein Auftrag für einen großen Sammel-PR.

### Core

| ID / Prio / Evidenz | Federführung → beteiligt | Kleinster Umfang und prüfbare Abnahme |
| --- | --- | --- |
| CORE-A01 / P1 / R+S | **Core/IO** → Runtime/Scene | Flush/Close-Fehler melden. Runtime-Szenenersatz über vollständig geschriebene Nachbardatei absichern; kein universeller Atomic-Write-Umbau. Gepufferter Fehler und fehlgeschlagener Ersatz dürfen keinen Erfolg/Dateiverlust ergeben. |
| CORE-A02 / P1 / S | **Core/Telemetry** → Graphics | Kategorieanlage/-zugriff synchronisieren. Exakte Samplezahlen, Registrierung/Read/Reset und Kapazität prüfen; zusätzlich Race-Nachweis nach unten stehender Nachweisentscheidung. Kein vorhandener TSan-Nachweis. |
| CORE-A03 / P1 / S | **Core/ResourcePool** → Graphics/RHI | Löschlistenprüfung und Maintenance unter konsistentem Lock; Remove/Maintenance unter Race-Instrumentierung prüfen; kein deterministisch roter reiner Funktionstest zugesagt, siehe Nachweisentscheidung. Pointer-Borrow bleibt eigener Vertrag. |
| CORE-A04 / P2 / R | **Core/LinearArena** → Geometry/EPA | Tatsächliche Adresse statt nur Offset ausrichten, Restkapazität/Überlauf prüfen. Überausrichtung mit absichtlich ungünstiger Basis testen. |
| CORE-A05 / P2 / R | **Core/ResourcePool** → Vulkan-Lifecycle | Wiederverwendbarkeit von Clear erhalten und Generationsgeschichte bewahren. Add→Clear→Add darf alten Handle nicht reaktivieren; Kapazitätsvertrag erhalten. |
| CORE-A06 / P2 / R | **Core/Process** → RUNTIME-282 | Deadline bis Prozessende durchsetzen, auch nach Pipe-EOF. Kind schließt Pipes und läuft weiter: Timeout beendet zuverlässig. Process nicht wegen weniger aktueller Caller löschen. |
| CORE-A07 / P2 / R | **Core/Dag und Tasks** → Graph-Consumer | Explizite Selbstzyklen konsistent ablehnen; Raw-DAG und TaskGraph vergleichen. Kein neuer Graph-Compiler als Voraussetzung. |
| CORE-A08 / P2 / S | **RUNTIME-325**, Core/Tasks → Runtime/Jobs | Queued/verworfene Coroutine-Frames genau einmal besitzen/freigeben. Blockierten Worker plus Frame-Parameter-Sentinel testen. Vor Adoption/Löschung bestehenden Operatorentscheid zum Consumer einholen; keine automatische Löschung. |
| CORE-A09 / P2 / R, Compiler | **Core/LocalTask** | Kopierbare benannte Lambda sowie move-only Rvalue unterstützen; beide genau einmal ausführen, Größen-/Alignmentgrenzen erhalten. |
| CORE-A10 / P2 / R | **Core/StringID** | Ordnung und Gleichheit auf dieselbe Identität beziehen; Debug-Container-/Vergleichstest und Release-Vertrag prüfen. |
| CORE-A11 / P2 / S | **Core-Testowner/CallbackRegistry** | Eigenes Datenrennen der Testfixture entfernen; Callback-Konkurrenz und alte Token-Auflösung mit verschiedenen Sentinels weiter prüfen. Die Fixture selbst benötigt Race-Nachweis, siehe Nachweisentscheidung; vor anderen Tests derselben Fixture korrigieren. |
| CORE-A12 / P3 / D | **Core/Telemetry** → Graphics-Diagnostik | Ungelesenen Samplepuffer nach Callersuche entfernen; benutzte Telemetrie erhalten. Nicht mit der Race-Reparatur gleichsetzen. |
| CORE-A13 / P3 / S | **Core/Graph-Diagnostik und Telemetry** | a: Kosten nicht als Nanosekunden ausweisen; b: Schedulerwerte bei Übergabe nicht verlieren. Je ein exakter Werte-/Einheitentest; keine Leistungsbehauptung. |

### Assets

| ID / Prio / Evidenz | Federführung → beteiligt | Kleinster Umfang und prüfbare Abnahme |
| --- | --- | --- |
| ASSETS-F01 / P2 / R | **Assets/EventBus und Service** → Runtime/AssetWorkflow | Nach Destroy im Callback keinen Ready-Erfolg mit bereits retirierter Payload zustellen; Callback-Reentranz und Live-Read prüfen. Kein belegter normaler Editor-Crash. |
| ASSETS-F02 / P2 / R | **Assets/Service** | Typ bei konkurrierendem Pfadtreffer erneut prüfen; Zweityp-Loads liefern TypeMismatch. Danach Thread-Affinität oder vollständig abgesicherte Publikation ausdrücklich entscheiden; Typprüfung allein beweist keine Thread-Sicherheit. |
| ASSETS-F03 / P2 / R | **Assets/ImportValidation** → Runtime/Materialisierung | Materialfaktoren auf Endlichkeit prüfen; gültige Fixture akzeptieren, NaN/Inf pro Faktor ablehnen. Pixelwirkung ist separate Abnahme. |
| ASSETS-F04 / P2 / S | **Assets/Loader-Lifetime** → Runtime/AssetWorkflow | Gespeicherte Loader dürfen ursprüngliche dekodierte Payload nicht unnötig festhalten. Benötigten Reload-Vertrag bewahren; Lifetime-Sentinel und Reload testen, keine ungemessene RAM-Ersparnis angeben. |
| ASSETS-F05 / P3 statt P2 / D | **Assets/Lifecycle und Requests** | Unbenutzten GPU-/Fence-Zustand und Request-Ballast löschen; reale Abbruch-/Schedulersemantik erhalten. GPU-Lifetime bleibt graphics/assets. |
| ASSETS-F06 / P3 statt P2 / S | **Assets/Reload** → Runtime-Aufrufer | Nach F04 eine vorhandene Reload-Transaktion wiederverwenden; Success/Failure/Cancel und bisherigen Current vor/nachher gleich testen. Kein neuer Service. |
| ASSETS-F07 / P2 / S | **Assets/Service-API** | Öffentliches LoadErased auf den benötigten privaten Helfer begrenzen; typisierte Aufrufer bleiben baubar. Kein produktiver externer Bypass belegt. |
| ASSETS-F08 / P3 / S | **Assets/Diagnostik** | Wiederholten Abschluss derselben ID so behandeln, dass jüngste Historie bleibt; exakte Folge testen. Bei Entfernung des ungenutzten Diagnosepfads unter F05 keinen Ersatz bauen. |

### ECS und die dort gefundenen Runtime-Fehler

| ID / Prio / Evidenz | Federführung → beteiligt | Kleinster Umfang und prüfbare Abnahme |
| --- | --- | --- |
| ECS-20261016-001 / P1 / R | **ECS/Hierarchy** → Runtime/Editor | Detach muss lokale/globale Pose konsistent aktualisieren; parentiertes Objekt ablösen und nächsten Hierarchie-Flush prüfen. |
| ECS-20261016-002 / P2 / R | **ECS/Transform und Hierarchy** | Nicht als TRS darstellbares Reparenting atomar ablehnen; Parent/Local/Global unverändert. Keine neue Scherungskomponente aus diesem Fix ableiten. |
| ECS-20261016-003 / P2 / R | **ECS/Transform** | Nichtendliche Translation vor Mutation ablehnen; NaN/Inf und gültige Positivkontrolle. |
| ECS-20261016-004 / P1 / R | **ECS/GeometrySourcesPopulate** → Runtime/GeometryIntegration | Kollision kanonischer Position-/Normal-Propertytypen mit bestehendem RemovePropertyIfPresent-Muster wie bei Mesh/Graph vor Publikation normalisieren; kein Abort/Teilzustand. Separat vom Sampling-Ausgabefehler. |
| ECS-20261016-005 / P2 / R+S | **ECS/Population** → Runtime/GeometryIntegration | Cloud-Löschmaske unter kanonischem Namen publizieren; downstream sichtbare/live Punkte testen. Andere Ursache als Geometry-Writer, der Slots statt Live-Elemente verwendet. |
| ECS-20261016-006 / P2 / R | **ECS/GeometrySourcesPopulate** | Cloud-Teilansicht konsistent materialisieren; Positionen, Properties und Domainzählung stimmen bei Nichtnull-Offset überein. |
| ECS-20261016-007 / P2 / R | **ECS/BoundsPropagation** | OBBs unter Spiegelung und relevanter Scherung korrekt konservativ bestimmen; alle transformierten Eckpunkte enthalten. |
| ECS-20261016-008 / P2 / S | **Runtime/RenderExtraction** → ECS/Bounds | OBB-Rotation in Welt-AABB berücksichtigen; rotierte Box und aus ECS-007 stammende Bounds testen. |
| ECS-20261016-009 / P1 / S | **Runtime/SceneSerialization** → ECS/Hierarchy | Abgewiesenes Attach beim Laden als Fehler behandeln, keine teilweise erfolgreiche Szene; ungültige Hierarchie laden. |
| ECS-20261016-010 / P2 / S | **Runtime/SceneSerialization und SceneDocument** | Geschwisterfolge durch Format, Snapshot und Load erhalten; direkte und queued Save→Load-Roundtrips mit mindestens drei Geschwistern, deren Folge von Entity-IDs abweicht. |
| ECS-20261016-013 / P3 / D | **ECS/Dokumentation** | Imports, Ownership und Statusbeschreibungen an aktuelle APIs angleichen; Links und Beispiele prüfen. |
| ECS-20261016-014 / P3 / D | **ECS/Selection** → Runtime-Picking | Ungenutzte zweite PickID-Oberfläche nach Callersuche entfernen; echte Pick-/Entityidentität erhalten. |

ECS-20261016-011 und ECS-20261016-012 sind vollständig den kanonischen Physics-Zeilen 02 und 03 zugeordnet. ECS-006 und GEO-09 teilen den Cloud-View-Vertrag: Geometry legt absolute Indizes und sichtbaren Bereich fest, ECS testet darauf die konsistente Materialisierung; beide Owner koordinieren diese Reihenfolge.

### Geometry

| ID / Prio / Evidenz | Federführung → beteiligt | Kleinster Umfang und prüfbare Abnahme |
| --- | --- | --- |
| GEO-AUD-01 / P1 / R+S | **Geometry/PointCloud** | Optionale Attributpointer bei Clear/Storageersatz invalidieren bzw. neu auflösen. Clear→Reuse sowie Copy/Move mit/ohne Attribute testen. Reproduziert ist die Speicherinvariante, kein tatsächlicher UAF. |
| GEO-AUD-02 / P1 / R+S | **Geometry/Graph.IO** → Runtime/Export | Alle Eingaben vor truncierendem Öffnen prüfen. TGF/EdgeList-Negativfälle lassen Sentinel-Datei intakt; keine pauschale Atomizitätszusage für alle IO-Fehler. |
| GEO-AUD-03 / P1 / R+S | **Geometry/GJK, EPA, ContactManifold** → Physics | Treffende kurze Simplexe korrekt für EPA aufbereiten; Boxpaar mit Solltiefe 0,5 plus echter World-Korrektur. Fallbackkonstante allein ändern genügt nicht. |
| GEO-AUD-04 / P2 / R | **Geometry/KMeans** → Runtime/Clustering, Graphics | Erstiteration/Konvergenz und Enddiagnostik konsistent machen. Seeds {0,100} bei {0,10,11}, Iterationslimit 1, Labels/Distanz/Inertia auf denselben Zentrenzustand prüfen. GPU separat abgleichen. |
| GEO-AUD-05 / P2 / R+S | **Geometry/PointCloud.IO und Utils** → Runtime/Noise | Live-Auswahl für Writer/Statistik und daraus abgeleitete Noise-Skala. XYZ/PLY/PCD, binär/ASCII, Attribute und all-deleted testen. GEOM-073 ist kein Fix-Owner hierfür. |
| GEO-AUD-06 / P2 / R+S | **Geometry/Triangle und MeshClosestFace** → Remeshing | Maßstabsbezogene Degeneration in Filter, Normalen und Projektion; skalierte gleichförmige Dreiecke. Blinden Fleck im Brute-force-Testoracle ebenfalls beseitigen. |
| GEO-AUD-07 / P2 / R | **GEOM-114 / Geometry-PCA** → Graphics/GLSL | Bestehenden relativen Cutoff-Task nutzen; Ebenen, Nullkovarianz, wiederholte Eigenwerte, Skalen und Eigenresiduen. CPU-/Shader-Verhalten getrennt nachweisen. |
| GEO-AUD-08 / P2 / R+S | **Geometry/RayCast und Overlap** | Nichtnormierte Rayrichtung in Kugelquadratik berücksichtigen; Faktoren 0,5/1/2, gleicher Treffpunkt, inverses t, Fehlschuss. Kein heutiger Produktionscaller belegt. |
| GEO-AUD-09 / P2 / R+S | **Geometry/Views und Conversion** | Absolute Handles, sichtbare Ranges und Remaps konsistent; Nichtnull-Offsets für Cloud und zweite Meshteilansicht. Keine neue Viewabstraktion. |
| GEO-AUD-10 / P2 / R | **Geometry/SparseGrid** | Logische Dimensionen vor Schlüsselpackung prüfen; Grenze und NX=2^24 mit NX+1 Vertices. Kein Aliasing durch akzeptierte Überkapazität. |
| GEO-AUD-11 / P2 / S | **Geometry/Mesh.Conversion** | Halfedge-/Edge-Attributverlust melden; alleinige h:texcoord/h:normal/e:feature-Fixtures. Corner-Remapping ist kein notwendiger Bestandteil dieses Diagnosefixes. |
| GEO-AUD-12 / P3 / S | **Geometry/PointCloud.Utils** | Zwei KDE-Einstiege nutzen gemeinsamen privaten Kern wie bestehendes Bilateral-Muster; äquivalente bestehende Tests vor/nachher. |
| GEO-AUD-13 / P3 / D | **GEOM-115 / Geometry** → Build/Dokumentation | Bestehende Operatorwahl zu Modul-/Dateinamen abwarten; ausschließlich mechanischer Diff mit Inventar-/Referenzupdate. Kein Blocker der fachlichen Fixes. |
| GEO-AUD-14 / P3 / S | **Physics/CollisionContacts** → Geometry-Scratch | Vorhandenen Scratch-Overload pro Kontaktberechnung wiederverwenden, Reset je Paar; identische Kontakte und gültiger Reset. Kein Performancegewinn behauptet. |
| GEO-AUD-15 / P3 / D | **Geometry/SparseGrid** | Nie befüllte Freelist samt wirkungslosem Zweig löschen; bestehende Gridtests unverändert grün. |

### Graphics und Visualisierung

| ID / Prio / Evidenz | Federführung → beteiligt | Kleinster Umfang und prüfbare Abnahme |
| --- | --- | --- |
| SRC-GRAPHICS-001 / P1 / S | **Graphics/GpuAssetCache** → Vulkan/Transfer | Ungültigen Uploadtoken vor Pending ablehnen, gültigen Current erhalten. Initialupload/Reload/Fallback mit operationalem ablehnendem Mock testen; nicht pauschal IsComplete({}) umdefinieren. |
| SRC-GRAPHICS-002 / P1 / A+S | **Vulkan/Staging und Transfer** | Voll-/Leerzustand und Pending-Reservierungen korrekt verwalten; Reserve–Copy–Submit/Retire–Collect konsistent machen. Beide Ringsequenzen gegen echte Implementierung; falls nötig kleinster CPU-testbarer Seam im vorhandenen Owner. Danach Threadvertrag prüfen. |
| SRC-GRAPHICS-003 / P1 / S | **Vulkan/Transfer** → RHI/TextureUpload | Vorhandene Uploadgrößen-/Alignmenthelfer nutzen; 8×8 RGBA8 mit 64 statt 256 Bytes vor Recording ablehnen. Gültige Formate testen, permissive Testfixture korrigieren. |
| SRC-GRAPHICS-004 / P1 / S | **Vulkan/Device** → Renderer | Zuerst erlaubte Backend-Sequenz zwischen EndFrame und nächstem Frame reproduzieren; ein produktiver Caller ist dafür nicht zwingend. DeferDelete an richtigen Submissionabschluss binden. In-/Zwischenframe-Freigabe und native Fence-Lifetime nachweisen. |
| SRC-GRAPHICS-005 / P1 / S | **Graphics/Renderer** → RHI-Testseam | Bei Transient-/Graphfehler vor Recording abbrechen. Buffer-/Texture-Erzeugung nach PrepareFrame fehlschlagen lassen: keine Passausführung, Succeeded=false. |
| SRC-GRAPHICS-006 / P2 / S | **Graphics/FrameRecipe** → Pass-Autoren | Nicht implementierte aktive Contributions ehrlich ablehnen; benötigte Formen tatsächlich bauen. Akzeptierte Metadaten müssen wirken. Kein neues Plugin-System. |
| SRC-GRAPHICS-007 / P2 / A+S | **Graphics/PropertyFilter** → Runtime/Smoothing | Reales Dispatchlimit beachten und vorhandene Kapazität durch Paging erhalten. 65.535/65.536-Gruppengrenze simulieren; keine übergroße einzelne Dispatch-Anweisung. |
| SRC-GRAPHICS-008 / P2 / A+S | **Runtime/Visualization** → Graphics/VisualizationSync | Eine finite skalenabhängige Range-Regel an beiden Stellen anwenden. Große ±Konstanten und größte finite Werte; keine leere Range. Gemeinsame Regel nur in zulässiger unterer Schicht, kein graphics→runtime-Import. |

### Physics und Runtime-Physics

Die Weltpose-Unterstützung bei PHYS-02 folgt dem [bestehenden Physics-Vertrag](../architecture/physics.md#transform-units-and-diagnostics). Parentierte Bodies generell abzulehnen wäre eine gesonderte Nutzerentscheidung, nicht der gewählte Standardfix. Die [Kollisionsgrenze](../architecture/physics.md#collision-contract-surface) hält ECS-/Runtime-Filterpolitik außerhalb von Physics; PHYS-03 erweitert diese Grenze nicht.

| ID / Prio / Evidenz | Federführung → beteiligt | Kleinster Umfang und prüfbare Abnahme |
| --- | --- | --- |
| PHYS-AUDIT-01 / P1 / R | **Physics/Solver** → rigid_body_reference | Kontaktgeometrie während Iterationen aktualisieren. Endabstand nach unten und oben begrenzen, Schwerpunkt und mehrere Iterationsbudgets prüfen; methodische Referenz mitkorrigieren, nicht bloß Parität testen. |
| PHYS-AUDIT-02 / P1 / S | **Runtime/PhysicsIntegration** → ECS, Physics | Kanonisch auch SRC-RUNTIME-005 und ECS-20261016-011. Weltpose hinein, Parent-Inverse beim Writeback; versetzte/rotierte/skalierte Parents, Weltgravitation und Parent-Bewegung nach Bodyanlage testen: Collider folgt, Writeback bleibt korrekt. Weltmatrix-/Fingerprintaktualisierung einschließen; TRS-ungeeignete Transformationen explizit behandeln. |
| PHYS-AUDIT-03 / P1 / S | **Runtime/PhysicsIntegration** → Physics | Kanonisch auch ECS-20261016-012. Filter-/Density-Absichten an Runtime-Grenze prüfen. Maske 0 über bestehende Teilnahme abbilden, wenn semantisch äquivalent, sonst ausdrücklich ablehnen; keine Kontakte/Korrektur trotz akzeptiertem Ausschluss. MassPolicy::Density und andere ununterstützte Nichtdefaults liefern Unsupported; normale Defaults funktionieren. Paarweise Masken in Physics brauchen eigenen Architekturentscheid. |
| PHYS-AUDIT-04 / P1 / R+S | **Physics/Sleep** → Runtime/Config | Wake bei kinematischem Stoß, Sleep-Deaktivierung, Gravitywechsel und entfernter Stütze. Erste zwei Fälle reproduziert, übrige zuerst als Regression ergänzen. |
| PHYS-AUDIT-05 / P1 / S | **Physics/Admission und Diagnostik** → Runtime/PhysicsIntegration | Dynamisch nicht uniforme Skalierung vor Simulation ablehnen; Kollisionsablehnung bis Runtime-Snapshot durchreichen. Body über Boden darf nicht still kontaktlos als gültig gelten. |
| PHYS-AUDIT-06 / P2 / R | **Physics/Validation und Integration** | Nichtendliche Ableitungen/Updates abfangen, letzten endlichen Zustand erhalten. Akzeptierte extreme Gravitation und Masse 1e-40 prüfen. |
| PHYS-AUDIT-07 / P2 / R | **Physics/BodyStorage** | Generationen über Clear bewahren. Add→Clear→Add: alter Handle darf weder lesen noch ändern oder löschen. Eigenständig gegenüber Core-Pool. |
| PHYS-AUDIT-08 / P2 / R | **Physics/CollisionShape** | Skala und Kindrotation korrekt komponieren. Orthogonale statische OBB bleibt unterstützt; echte Scherung explizit ablehnen. Positive/negative Kontakte des Auditbeispiels prüfen. |
| PHYS-AUDIT-09 / P2 / S | **Runtime/PhysicsIntegration** | Laufende Geschwindigkeit bei reinen Masse-/Collideredits erhalten; gezielten Authoringzustand vergleichen. Explizite Geschwindigkeitsänderung weiter anwenden. |
| PHYS-AUDIT-10 / P2 / S | **Runtime/Kinematik** → Physics | Bestehende externe Posehoheit durchgängig machen. Sichtbare ECS-Pose und Colliderort über mehrere Frames identisch; keine still simulationsgetriebene Kinematik einführen. |
| PHYS-AUDIT-11 / P2 / R | **Physics/Solverdiagnostik** | Nur lösbare dynamische Kontakte für Konvergenz bewerten. Statisch–statisch/kinematisch ohne dynamische Insel erzeugt keinen falschen Iterationsfehler. |
| PHYS-AUDIT-12 / P2 / S | **Runtime/PhysicsIntegration** | Ohne Transform keinen Body erzeugen; alte Bindung beim Entfernen des Transforms abbauen. Keine Geisterkontakte am Ursprung. |
| PHYS-AUDIT-13 / P2 / S | **Physics/API** → Runtime-Caller | Unbenutzten mutablen GetBody-Overload entfernen; const-Lesen bleibt, UpdateBody validiert/weckt. Keine stabilen Borrow-Pointer versprechen. |
| PHYS-AUDIT-14 / P3 / S | **Physics/Integration** | Gemeinsamer privater Kern für Step/SolveStep, jeweilige Sleep-/Solve-Semantik erhalten. Mit 06 nur bündeln, wenn für dieselbe Absicherung nötig. |
| PHYS-AUDIT-15 / P3 / D | **Physics/Dokumentation und Tests** | Totes Motion-Feld, falsche Aussagen/fehlende Synopsis/Backlogindex korrigieren; Runtime-genutztes IsFinite(vec3) erhalten. Hinweise des Scanners nicht als zusätzliche Bugs zählen. |

### Platform und die dort gefundenen Runtime-Fehler

| ID / Prio / Evidenz | Federführung → beteiligt | Kleinster Umfang und prüfbare Abnahme |
| --- | --- | --- |
| PLAT01 / P1 / R+S | **Platform/LocalSocket** → Runtime/Agent | send selbst nonblocking; EAGAIN über bestehende Pollschleife. Nichtlesender Peer, große Antwort und äußerer Watchdog ergeben Timeout; Sandbox-Shutdown separat nachweisen. |
| PLAT02 / P2 / R | **Platform/LocalSocket** | Nur explizit stale/Closed ersetzen, SystemError erhält aktiven Pfad. Isolierte FD-Erschöpfung testen. |
| PLAT03 / P2 / R | **Platform/LocalSocket** | Eigentümeridentität vor unlink prüfen und beim Move übertragen. Ersatzdatei/Nachfolgesocket bleiben bei altem Close erhalten; kein adversariales Race-Sicherheitsversprechen. |
| PLAT04 / P2 / R+S | **Platform/Input** → Runtime/Input | Press-Flanke unabhängig vom Endzustand speichern. Down+Up im selben Poll genau einmal erkennbar; Hold/Repeat erzeugt keine künstlichen Flanken. |
| PLAT05 / P2 / S | **Runtime/Kernel** → Platform, ImGui | Eventverbrauch/-Discard nach optionalen Konsumenten eindeutig besitzen. Mehrere Frames ohne EditorUi lassen nichts wachsen; UI-Release/Focus/Minimize/Restore bleiben verfügbar. |
| PLAT06 / P2 / S | **Platform/GLFW** → Runtime/Engine | Nativen X11-Minimize/Restore-Repro zuerst; danach Iconify berücksichtigen. Null-Tests sind kein nativer Nachweis, GLFW_ICONIFIED kein pauschaler Wayland-Fix. |
| PLAT07 / P2 / R | **Platform/Null** | Pending-Batch vor Iteration entnehmen; Callback-Event bleibt bis zum nächsten Poll. Kein beobachteter produktiver UAF behauptet. |
| PLAT08 / P2 / S | **Platform/GLFW** | Owning Window noncopyable machen; Compile-time-Prüfung in GLFW-fähigem Target/Preset, ohne Displaybedarf und ohne neue Move-Funktion. Kein In-tree-Copycaller belegt. |
| PLAT09 / P2 / S | **Runtime/ImGui** | Falsche HasMouseCursors-/HasSetMousePos-Flags entfernen und testen. Keine ungefragte Cursor-/Warp-Implementierung. |
| PLAT10 / P2 / S | **Runtime/AgentServer** | Jede fertige Zeile vor Queueing auf Limit prüfen, Restpufferprüfung behalten. Grenze/+1 sowie mehrere Receives; keine unbegrenzte Nachrichtengröße behaupten. |
| PLAT11 / P3 / D | **Platform/API** → Runtime-Testdoubles | Ungenutzte Aliase/Zustände entfernen; PollEvents, ScrollDelta und echte ApplyEvent-Consumer erhalten. Caller und Testdoubles gemeinsam aktualisieren. |
| PLAT12 / P3 / D | **Platform/Dokumentation** | Drei fehlende Synopsen und LocalSocket-README korrigieren; Diagnosewissen verlinken. Sieben Heuristikhinweise nicht als bestätigte Defekte zählen. |

### Runtime

| ID / Prio / Evidenz | Federführung → beteiligt | Kleinster Umfang und prüfbare Abnahme |
| --- | --- | --- |
| SRC-RUNTIME-001 / P1 / R | **Runtime/PointSamplingOperations** | Vorhandenes ValidatePointOutputs für beide typisierten Ausgaben in Preview und Apply verwenden. Falscher Rank-/Selected-Typ: kein Abort, keine Property-/History-Änderung. |
| SRC-RUNTIME-002 / P1 / R+S | **Runtime/History und Property-Publication** | Async-Stale- und History-Replay-Prüfung unterscheiden. A→B→Undo→Undo→Redo→Redo plus abgelehnter fremder Edit; Sampling, Scalar und Positionspfad einzeln prüfen. Revisionen nicht pauschal ignorieren. |
| SRC-RUNTIME-003 / P1 / R | **Runtime/SceneDocument und History** | Snapshotrevision statt späterer aktueller Revision als gespeichert markieren. Edit während queued Save bleibt dirty; Save-Reihenfolge auf gleichen Pfad zusätzlich prüfen. |
| SRC-RUNTIME-004 / P2 / S | **Runtime/SceneDocument und Serialization** | VertexChannelBindingSet in Snapshot übernehmen; queued Save→Load erhält Bindung und Property. Kein generisches Serializerframework nötig. |
| SRC-RUNTIME-006 / P2 / S | **Runtime/Engine und RenderExtraction** → Graphics | Beim Worldwechsel alle konsumierbaren Snapshot-Slots/Poolhistorie invalidieren. Front in Slot 1, Wechsel zur leeren Welt, Pipelineframe ohne alte Renderables/Lichter/Selection. Kein GPU-UAF belegt. |
| SRC-RUNTIME-007 / P2 / S | **Runtime/AgentProtocol und UndoRedo** | Nichtobjekt-Argumente sowie vorhandenes ungültiges steps ablehnen; Default nur bei fehlendem Feld. Negative/String/Array/Overflow mutieren weder Szene noch History. |
| SRC-RUNTIME-008 / P2 / R | **Runtime/OutlierConfig** | Positive Float-Darstellbarkeit nach Konvertierung prüfen. 1e-100 zurückweisen; jeder akzeptierte kanonische Payload ist erneut validierbar. Vorhandenen Prüfmechanismus nutzen. |
| SRC-RUNTIME-009 / P2 / S | **RUNTIME-276 / Runtime-Config** | Fehlende Familien auf vorhandene ConfigFieldSpec migrieren; Schema/Validator stimmen überein, Cross-Field-Checks bleiben. String-/Integer-Enum-Format vor Änderung ausdrücklich auflösen. |
| SRC-RUNTIME-010 / P2 / S | **Runtime/JobService** → CPD-Consumer, Core/Scheduler | Tatsächlich angeforderte Priorität wirksam machen; deterministische Reihenfolge konkurrierender Jobs und Worker-only-Ausführung. Kein nachgewiesener Deadlock; Kind/Cost erst nach Callerprüfung entfernen. |
| SRC-RUNTIME-011 / P2 / S | **Runtime/RenderWorldPool** | Vorzugsweise tatsächlichen seriellen Main-Thread-Vertrag dokumentieren/durchsetzen. Nebenläufigkeit nur bei realem Consumer implementieren; heutiger Engine-Caller ist seriell. |
| SRC-RUNTIME-012 / P3 / S | **Runtime/JobService-Diagnostik** | Expliziten Pending-Cancel nach Dependency-Drain genau einmal zählen, Ursache korrekt; exakte Deltas statt >=1. |
| SRC-RUNTIME-013 / P3 / D | **Runtime/Dokumentation** | Überholte Persistenzbeschreibung von ReferenceScene entfernen; README auf aktuelle Zuständigkeiten und verlinkte Verträge kürzen. |
| SRC-RUNTIME-014 / P3 / D | **Runtime/Editor-Operations** → Build/Dokumentation | Basenames bei eigener mechanischer Bereinigung an tatsächliche Owner angleichen; Modulnamen/APIs unverändert, CMake und Referenzen nachführen. |

SRC-RUNTIME-005 ist vollständig PHYS-AUDIT-02 zugeordnet.

## Bereits vorhandene Tasks weiterverwenden

| Owner / bestehender Task | Behandlung und Abhängigkeit |
| --- | --- |
| **CI:** [BUG-244](../../tasks/done/BUG-244-cpu-selection-rejects-source-rules-producer.md), [BUG-245](../../tasks/backlog/bugs/BUG-245-nightly-smoke-backend-manifest-mismatch.md) | Einmalige Owner der in mehreren Berichten genannten CI-Probleme. Keine acht weiteren CI-Aufgaben. |
| **Geometry:** [GEOM-114](../../tasks/done/GEOM-114-scale-relative-pca-isotropic-cutoff.md), [GEOM-115](../../tasks/done/GEOM-115-halfedge-module-names-match-file-stems.md) | Direkte Owner für GEO-07 bzw. GEO-13. Naming-Entscheidung bleibt beim Operator. |
| **Geometry:** [GEOM-073](../../tasks/backlog/geometry/GEOM-073-point-analysis-property-span-contracts.md), [GEOM-074](../../tasks/backlog/geometry/GEOM-074-graph-property-adjacency-contracts.md) | Verbleibende Borrow-/Span-Seams, kein Ersatz für neue Löschmasken-, Export-, View- oder Numerikfixes. Bereits gelieferte Teile nicht neu planen. |
| **Runtime:** [RUNTIME-276](../../tasks/done/RUNTIME-276-declarative-config-field-specs.md), [RUNTIME-325](../../tasks/backlog/runtime/RUNTIME-325-coroutine-waits-in-jobservice.md), [RUNTIME-282](../../tasks/backlog/runtime/RUNTIME-282-headless-batch-cli.md) | Configschema; Coroutine-Consumerentscheidung; vorgesehenes Process-Modul. Vorhandene Verträge verhindern vorschnelle Neubauten/Löschungen. |
| **Runtime/Processing:** [RUNTIME-274](../../tasks/active/RUNTIME-274-point-cloud-subsampling-workflow.md), [RUNTIME-294](../../tasks/active/RUNTIME-294-migrate-gpu-methods-to-property-residency.md), [RUNTIME-311](../../tasks/backlog/runtime/RUNTIME-311-unify-gpu-scalar-outlier-transaction-lifecycle.md), [RUNTIME-314](../../tasks/backlog/runtime/RUNTIME-314-reuse-existing-processing-helpers.md) | Bestehende Erweiterungen, Residency-Slices und verbleibende Nachweise erhalten. Bereits gelandete Konsolidierungen nicht wiederholen; Sampling-Absturz/Undo bleiben eigenständige Fixes. |
| **Graphics/Lifetime:** [GRAPHICS-157](../../tasks/backlog/rendering/GRAPHICS-157-bindless-slot-completion-retirement.md), [BUG-232](../../tasks/backlog/bugs/BUG-232-gpuworld-commit-copy-lost-on-failed-submit.md) | Mit Welle 1b koordinieren, je eigener Regression/Abnahme: Descriptor-Reuse bzw. Commit-Copy nach fehlgeschlagenem Submit. Kein Duplikat von G004. |
| **Graphics/Material:** [GRAPHICS-105](../../tasks/active/GRAPHICS-105-unified-mesh-shading-and-attribute-source-authority.md) | Materialfaktor-/Texturprojektion und Attribute-Authority haben bereits einen aktiven Owner. Assets validiert Payloads, Runtime materialisiert, Graphics besitzt GPU-Darstellung. |
| **Graphics/Wartung:** [GRAPHICS-153](../../tasks/active/GRAPHICS-153-gpu-buffer-reuse-and-minimal-io.md), [GRAPHICS-164](../../tasks/backlog/rendering/GRAPHICS-164-pipeline-init-block-helpers.md) | Bestehende GPU-Reuse-Arbeit bzw. Pipeline-Init-Konsolidierung. G007 ist dadurch nicht automatisch erledigt. Init-Cleanup nach Fehlerfixes. |
| **Assets/Runtime-Import:** [ASSETIO-009](../../tasks/backlog/assets/ASSETIO-009-loss-aware-materialx-subset-import.md), [ASSETIO-010](../../tasks/backlog/assets/ASSETIO-010-async-model-companion-preflight.md), [ASSETIO-011](../../tasks/backlog/assets/ASSETIO-011-semantic-sandbox-file-import-workflow-matrix.md) | MaterialX hängt an GRAPHICS-105; File-/Import-Workflow an Companion-Preflight. Keine neuen Import-Service-Umbrellas. |
| **Physics:** PHYSICS-005/006/007/008 im [Physics-Backlog](../../tasks/backlog/physics/README.md) | Nichtstarre/Vulkanarbeit bzw. Broadphase/k-DOP bleiben eigene Produktarbeit; sie besitzen keinen der neuen Rigid-Body-Fixes automatisch. |
| **Runtime/App-Export:** [UI-046](../../tasks/done/UI-046-sandbox-geometry-export.md) | Exportworkflow nutzt vorhandene Geometry-Writer. Writerfix GEO-02 bleibt Geometry; Pfadauswahl ist kein Anlass für einen zweiten Writer. |
| **Native UI/Platform:** [UI-079](../../tasks/active/UI-079-imguizmo-hidpi-operational-run.md) und [PLATFORM-004](../../tasks/backlog/platform/PLATFORM-004-alternative-platform-backend-onboarding.md) | HiDPI-Nachweis bzw. Planung weiterer Plattformen bleiben getrennt von PLAT06; keine neue Backendimplementierung aus diesem Audit ableiten. |

## Historische Berichte und unnummerierte Empfehlungen

Die Mai-Geometry-Analyse nennt GEOM-005 bis GEOM-012. Diese Tasks liegen inzwischen im Archiv; einige Abschlüsse betreffen nur Foundation oder Roadmap. Die damaligen Aussagen über fehlende Soup-/View-/Linalg-/Benchmarkgrundlagen sind daher keine aktuelle offene Liste. Ebenso sind im ECS-Bericht HARDEN-061 bis HARDEN-068, RUNTIME-091 und PHYSICS-001 bis PHYSICS-003 historische, abgeschlossene Arbeit. Ihr Abschluss beweist weder heutige Fehlerfreiheit noch sämtliche später erwogenen Erweiterungen.

Alte Kandidaten für Optimierung, exakte/adaptive Prädikate, volumetrische Strukturen, weitere Remeshing-/PDE-/Rekonstruktionsmethoden bleiben **historisch und bedarfsabhängig**. Das gilt ebenso für damaliges Light-Authoring, Enabled/Visibility, Selection-Commands, DEC-Caches und die nur identifizierten HARDEN-068-Impl-B/C; Taskabschluss ist kein Liefernachweis dieser Optionen. Diese Synthese nimmt keine neue Vollinventur aller damaligen Forschungsideen vor. Neue Features nur über aktuelle Produkt-/Methodenowner auswählen. Alte Empfehlungen für Compatibility-Shims werden vom heutigen Repositoryvertrag überholt.

| Empfehlung ohne eigene aktuelle Befund-ID | Zuständigkeit und Entscheidung im Plan |
| --- | --- |
| Core.Base64-Implementierung im Interface; fehlende Synopsen; veraltete ReferenceScene-README | **Core**, Welle 3: nichttriviale Bodies privat ablegen, konkrete Synopsis-/Dokufehler korrigieren. 82 Scannerhinweise sind keine 82 bestätigten Bugs. |
| Zwei teilweise doppelte Core-Graphcompiler (CORE-A07/A13a) | **Core/Tasks und Dag**, nach lokalem Zyklusfix: vorhandene Caller-/Datentypverträge entscheiden, dann nur gemeinsame benötigte Mechanismen konsolidieren. Kein dritter Graph-Layer; API-Entscheidung bleibt ausdrücklich offen. |
| Assets-Typkennungsfaltung und GeometryKind-Duplikat | **Assets**, Welle 3: Asset.TypePool bzw. vorhandenen IsGeometryPayloadKind wiederverwenden; keinen Wrapper hinzufügen. Borrow-Lifetime und tatsächlichen Runtime-Byte-Transport dokumentieren. AssetIO-/Decode-Stagetrails messen wegen des vorangestellten Loaders keine tatsächliche I/O-/Decode-Laufzeit. |
| Große Rendererdateien, Compute-Gruppierung, Vulkan-Bootstrap | **Graphics**, nach Welle 3 optional: erst konkrete Wartungsgrenze zeigen, dann eine vorhandene Passfamilie verschieben. Keine automatische Neuorganisation; GRAPHICS-164 für echte Init-Duplikate verwenden. |
| Falscher GpuAssetCache-Vertragskommentar, Renderer-README, Capture-Budget-Kommentar | **Graphics/Runtime**, Welle 3: sachlich korrigieren. Kein Race oder Capture-Hang allein aus Kommentar ableiten. |
| Physics-Collision-Dateisplit, Geometry PointCloud.Utils-Aufteilung | **Physics/Geometry**, nur bei verbleibendem Wartungsnutzen: getrennt von Semantikfixes, Module erhalten. KDE-/Scratch-Fixes benötigen keinen vorausgehenden großen Split. |
| Reine Schreibstil-/Namensvarianten und Backend-Verzeichnisasymmetrie | **Jeweiliger Owner**: bei tatsächlichem fachlichem Eingriff lokal angleichen; keine flächige Formatierungs-/Umbenennungskampagne. |

Prüfspuren ohne bestätigten neuen Defekt bleiben separat: Core-Arenamarker/OOM-/Borrow-Verträge, Socket-Connect bei gefülltem Backlog, externe Nutzung von TextureManager::Reupload und nicht verifizierte Plattformzweige. Owner jeweils Core, Platform bzw. Graphics; erst bei konkreter Beauftragung reproduzieren oder Vertrag klären, nicht still als Fixarbeit aufnehmen.

Nicht neu als Bugs einplanen: verworfener Agent-Capture-Hang beim Minimieren; genereller GLFW-Fokusverlust mit hängenden Tasten; ausdrücklich gemeldeter UV-Verlust bei Subdivision; fehlende Physics-Serialisierung mit UnsupportedPhysicsEntities; partielle Mesh-CSG und Simplification C/D als ausdrücklich unsupported; fehlende IME-/Multiwindow-/Native-Dialog-Funktionen ohne entsprechenden Auftrag. Gezielte Produktlücken können trotz ehrlicher Unsupported-Diagnose bestehen, gehören aber zu den bestehenden Produktentscheidungen.

## Abnahme und erste Verifikationsbefehle

Für jeden späteren Verhaltensfix: Trigger zuerst als Sollassertion gegen unveränderten Code ausführen; anschließend Fix und Rot-/Grün-Nachweis, bei neuer Testlogik zusätzlich zeigen, dass Entfernen der entscheidenden Änderung den Test wieder scheitern lässt. Kein Test darf nur eine bekannte falsche Ausgabe festschreiben. Refactoringtests laufen vor und nach dem Umbau. Exakte betroffene Targets über `build/ci/test-inventories/RegisteredTestSources.tsv` auflösen; keine pauschale Vollsuite für kleine Slices.

Der erste Slice SRC-RUNTIME-003 verwendet das bereits zugeordnete Testtarget:

```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^SceneDocumentModule\.'
```

Vorher die deterministische Queued-Save-Regression aus dem Runtime-Bericht in den zuständigen Test übernehmen. Für Hierarchie-/Serializer- oder andere später hinzugezogene Caller die passende Suite ergänzen; ein grüner Save-Test deckt CORE-A01 oder die fehlenden Bindings nicht automatisch ab. Für Core-IO ist vor Beauftragung die konkrete neue Regression einem existierenden Core-Testtarget zuzuordnen.

Bei src-Änderungen zusätzlich die vorgeschriebenen Layering-/Strukturprüfungen; bei Interfaces Inventar und Caller aktualisieren. Native Vulkan-/GLFW-/Sanitizerbelege sind eigene Evidenzklassen. Null-/Mocktests beweisen keine operative GPU-Reparatur. Ergebnisse für Performance, Parität oder Operational-Claims erfordern bei späteren Runs den bestehenden Experiment-Preflight; hier wurden keine solchen Runs gestartet.

Offene Entscheidungen betreffen jeweils nur ihr Paket: RUNTIME-325-Consumer, GEOM-115-Namenspolitik, RUNTIME-276-Enumformat, ASSETS-F02-Threadvertrag und gegebenenfalls eine Erweiterung der Physics-Filterpolitik. Bei CORE-A07/A13a ist vor späterer Compilerkonsolidierung die benötigte API-Oberfläche zu entscheiden; der lokale Selbstzyklusfix kann vorher erfolgen. Hinzu kommt die unten erläuterte Race-Nachweisentscheidung. Andere Fixes warten darauf nicht. Zeigt eine Reparatur eine zusätzliche Fehlerlage außerhalb des beschriebenen Falls, an dieser Grenze stoppen, neuen Umfang benennen und vor dessen Umsetzung den Operator fragen.

### Noch zu vereinbarender Nachweisweg für Race-Fixes

CORE-A02, CORE-A03 und CORE-A11 sind bisher Quellbefunde. Ein gewöhnlicher Paralleltest, der zufällig grün bleibt, erfüllt weder den Race-Nachweis noch die Rot-/Grün-Regel. CORE-A02 braucht exakte Zähler-/Registrierungsassertions zusätzlich zur Race-Prüfung; CORE-A03 braucht Remove gegen Maintenance; CORE-A11 muss die Testfixture selbst prüfen.

Der aktuelle Build bietet ASan/UBSan, aber **kein TSan-Preset bzw. keinen TSan-Sanitizermodus**. Deshalb sind diese drei Pakete hinsichtlich der Nachweisstrategie noch nicht startbereit. Empfehlung für ihre Beauftragung: einen eng begrenzten TSan-Diagnoseweg gegen die echten betroffenen Implementierungen und einen bekannten Race-Kontrollfall vereinbaren, ohne pauschalen Toolchain- oder CI-Umbau. Ob die vorhandene Toolchain diesen Weg trägt, ist zuerst zu prüfen. Für einen nötigen Repository-Sanitizerausbau ist ein eigener Scopeentscheid erforderlich. Falls der Operator stattdessen Quellbeweis plus unabhängigen Review als Ausnahme akzeptiert, muss er diese Ausnahme ausdrücklich entscheiden; hier wird keine erteilt. Bis dahin keine deterministisch rote Funktionstest-Abnahme behaupten. Dies betrifft nur diese Pakete, weder den Abschluss der Planung noch andere Fixes.

## Prüfung dieser Planung

CI am 16. Oktober 2026 frisch gelesen: [ci-linux-clang](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38869189381) am Audit-HEAD rot, [ci-docs](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38869189384) grün; letzter [nightly-deep](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38854737500) auf älterem Stand rot. Die Fehlerursachen aus BUG-244/245 wurden als bestehender Kontext übernommen; keine numerischen Enginefehler aus roten CI-Läufen abgeleitet.

In dieser Planung: Berichtvollständigkeit, Originalkennungen, echte Duplikatgruppen, aktuelle Taskpfade/-zustände und Testtarget für den ersten Slice geprüft. Drei lesende Analyseagenten deckten Core/Assets/ECS, Geometry/Physics und Graphics/Platform ab; Runtime wurde vom koordinierenden Agenten vollständig gelesen. Sie änderten keine Dateien und starteten keine Builds oder Tests; Read-only und Delegationsverbot waren instruktionsbasiert.

Ausgeführt: `check_task_policy.py --root . --strict` (638 Tasks, null Befunde), `check_doc_links.py --root . --strict` (keine defekten Links), Abgleich aller 99 Originalkennungen und 96 eindeutiger kanonischer Tabellenzeilen sowie Whitespaceprüfung der beiden bearbeiteten Dokumente. Ein beim ersten Linkcheck gefundener falscher PLATFORM-004-Dateiname wurde korrigiert. Historische Audit-Tests wurden nicht wiederholt; Live-Sandbox, Vulkan, GLFW, Sanitizer, Benchmarks und vollständige CPU-Suite wurden nicht ausgeführt. Aus den Berichten folgt keine neue Leistungs- oder Paritätsaussage.

Unabhängiges Review am 2026-10-16, Codex → Claude Code; angefordert `claude-opus-5-5`, Effort `max`, Modell durch Ergebnis und Nutzungsmetadaten bestätigt, Effort vom Reviewer nicht unabhängig sichtbar. Session `baff3573-bc28-4b0a-8878-3af9894c64a5`. Read-only/Delegationssperre technisch durch `--strict-mcp-config --tools Read,Grep,Glob` durchgesetzt. Erstes Paket SHA-256 `1f836fdfa9090fb1ea2017e87aaa646826f2421cee98e4be05c1c8969f8bca42`; geprüfter Plan `710454104672b94573160fe75261966747c858e48f4917f1a48d0a6cd0bbfbd7`, Index `83a032cc5f315a157ed1251e0a7ae5e59fa4f53bf62f33419ca5658b7c6b1ff6`. Der Reviewer nutzte diese vom Koordinator gelieferten Hashes und rechnete sie mangels Shellzugriff nicht selbst nach.

Erstes Urteil wörtlich: **„revise“**. Pflichtkritik wörtlich: „Die zusammengeführte Physics-Hierarchiezeile verliert einen Auslöser.“; „Die Liste offener Entscheidungen […] ist unvollständig.“; „Die Abnahme für ECS-010 deckt den Editor-Speicherpfad nicht ab.“; „Die Abnahmen der Race-Befunde sind nicht erfüllbar und wirken so als versteckter Gate.“ Die Überarbeitung ergänzt Parent-Bewegung und queued Save, wahrt die bestehende Runtime-Filterpolitik und benennt fehlende Vertrags-/Race-Nachweisentscheidungen. F07 bleibt P2; die Begründung der F06-Rückstellung berücksichtigt ihre realen Runtime-Aufrufer. Der ergänzte Syntheselink im vorbestehenden Index ist beabsichtigt; seine Berichttabelle bleibt unverändert.

Delta-Review in derselben Session: Paket SHA-256 `dd9f38581a004ea21907cde378846b701c9ae1f8d285d923ae332b7bd7bd416d`, geprüfter Plan SHA-256 `85875d052315e43cb33f631f58c95febd9c13ebea22bbda482a7e4558b6690da`, Index unverändert. Endgültiges Urteil wörtlich: **„approve“**. Der Reviewer bewertet alle sieben Kriterien als erfüllt: Berichtsabdeckung, Deduplikation, Priorisierung, Zuständigkeiten, keine Implementierung/Taskanlage, aussagekräftige Abnahmen und korrekte Evidenzkennzeichnung. Seine Einschränkung bleibt: „Die Race-Pakete bleiben blockiert, bis über den Nachweisweg entschieden ist.“ Damit ist die Einordnung in Welle 1b eine Priorität, keine Startfreigabe. Der nach den neuen Physics-Ankern ausgeführte strikte Linkcheck bestand mit 4.588 geprüften relativen Links; damit ist auch die entsprechende Reviewbedingung erfüllt.

Der Review bindet den inhaltlichen Stand vor Eintragung dieses Ergebnisses; anschließend wurden ausschließlich diese Reviewmetadaten ergänzt. Temporäre Pakete und Antworten wurden nach Übernahme der Entscheidungen entfernt; die ursprüngliche, weiterhin benötigte Evidenz der acht Bereichsaudits bleibt unangetastet.
