# Geometry Audit für die gemeinsame src Auswertung

## Nutzerfragen und Auftrag

1. „sollten wir geometry aufräumen?“
2. „anders organisieren?“
3. „hat die implementierung einen durchgenden stil?“
4. „gibt es offene punkte, bugs, implementierungs gaps?“

Der ursprüngliche Auftrag verlangte das Ponytail-Plugin und Subagenten aus
verschiedenen Use-Case-Rollen. Der anschließende Dokumentationsauftrag lautet:

> Erstelle einen Bericht aus deinen Ergebnissen in IntrinsicEngine so dass Codex 6 Astra Ultra anschließend alles finden kann. Ich lasse für alle weiteren unterordner von IntrinsicEngine/src von anderen Agenten ebenfalls so einen Bericht erstellen

Dieser Bericht ist der Einstieg für die spätere gemeinsame Auswertung.
`GEO-AUD-01` bis `GEO-AUD-15` sind stabile Befundkennungen dieses Berichts,
keine neu angelegten Task-IDs oder Implementierungsfreigaben. Nummern 01–11
entsprechen der ursprünglichen Audit-Antwort; 12–15 ihren Cleanup-Empfehlungen.

Bedientes Repository-Ziel: [Framework24-Konvergenz](../product/framework24-convergence.md)
durch verlässliche Geometry-Workflows, besonders Import/Bearbeitung/Export
(W2/W4), numerisch korrekte Verarbeitung und ehrliche Fehlerdiagnostik (W3/W6).
Konkrete Vertragsfehler zu beheben ist dafür kürzer als eine flächige Neuorganisation.

## Urteil und Prüfstand

**Gezielt aufräumen; keine große Ordner-, Namespace- oder Abstraktionsreform beginnen.**
Der Geometry-Layer, schmale C++-Module, gemeinsame Property-Speicher und die
Trennung schwerer Implementierungen von Interfaces sind brauchbare Grundlagen.
Die schwersten Unterschiede betreffen Lebensdauer, lebende Elemente gegenüber
Speicherplätzen, Fehlerpfade und numerische Größenordnungen.

| Feld | Stand |
| --- | --- |
| Datum | 2026-10-16, Europe/Berlin |
| Repository / Branch | IntrinsicEngine / `main` |
| Geprüfter Commit | `66da33fd6bfabb20408607b8361354fc9adb420d` |
| Umfang | `src/geometry` sowie ausgewählte Aufrufer, Tests und Aufgaben |
| Bestandszählung | 252 C++-Dateien: 132 `.cppm`, 117 `.cpp`, 3 `.hpp`; 88.150 Zeilen einschließlich Unterordner |
| Nutzung | Interaktive Desktop-Geometrie, wiederholte Bearbeitung/Speicherung, unterschiedliche Modellmaßstäbe, gewöhnliche Physik-Kollider |
| Enginecode | Beim Audit sauber; durch Audit und Bericht nicht geändert |
| Toolchain | `ci`, Clang 23 und passendes `clang-scan-deps-23`, Debug, Sanitizer aus |

Zeilenangaben beziehen sich auf diese Revision; Dateipfade und Symbole bleiben
Suchanker bei Verschiebungen. Vor Reparaturen aktuelle Quellen und Tasks lesen.

**R** bedeutet im echten C++-Pfad gegen die frisch gebaute Bibliothek reproduziert.
**S** bedeutet im Quell-/Aufruferpfad geprüft, ohne zusätzlichen passenden Lauf.
**K** bezeichnet einen bereits bekannten offenen Punkt. Keine Klasse ist ein
GPU-, Performance- oder vollständiger Produktreife-Nachweis.

| Reviewer | Perspektive |
| --- | --- |
| `/root/geometry_api_usecases` | API-Nutzer, Container-Lebensdauer, Import/Export, Properties, Editor-Operationen |
| `/root/geometry_numerics` | Queries, numerische Robustheit, räumliche Indizes, Physik |
| `/root/geometry_maintainer` | Neue Mitwirkende, Organisation, Stil, Duplikation, Erweiterung und Tests |

Die unabhängigen ersten Rollenprüfungen wurden danach zusammengeführt.
Die Subagenten änderten keine Dateien und delegierten nicht weiter; diese
Beschränkungen waren instruktionsbasiert. Der Koordinator schrieb und führte
die Repros aus. Der Numerik-Reviewer prüfte anschließend Repro und Log und
präzisierte die SparseGrid-Grenze sowie die K-Means-Assertion. Modell/Effort
wurden geerbt; die genaue Identität wurde nicht unabhängig bestätigt. Astra Ultra
bezeichnet den gewünschten späteren Leser, nicht eine behauptete Revieweridentität.

## Befundübersicht

P1 priorisiert Speicher-/Dateisicherheit und einen tatsächlich verwendeten
Physikvertrag; P2 konkrete Ergebnis-/API-Fehler mit teils begrenzter
Produktionsreichweite. P3 sind Wartungsverbesserungen ohne gemessenen Performancegewinn.

| ID | Prio | Befund | Evidenz | Vorhandener Owner / Nachbarbereich |
| --- | --- | --- | --- | --- |
| GEO-AUD-01 | P1 | PointCloud hält zerstörte Attributspeicher für gültig | R/S | Container-Lebensdauer |
| GEO-AUD-02 | P1 | Graph-Export leert bestehende Dateien vor Validierung | R/S | Assets/Export abgleichen |
| GEO-AUD-03 | P1 | Boxkontakt liefert falsche konstante Eindringtiefe | R/S | Physics abgleichen |
| GEO-AUD-04 | P2 | K-Means: falsche Konvergenz, inkonsistente Enddiagnostik | R | Runtime-Clustering abgleichen |
| GEO-AUD-05 | P2 | Gelöschte Punkte bleiben in Export und Statistik wirksam | R/S | GEOM-073 nur benachbarter Seam; kein identifizierter Fix-Owner |
| GEO-AUD-06 | P2 | Kleine wohlgeformte Dreiecke werden zu Kanten reduziert | R/S | Remeshing/Referenzprojektion |
| GEO-AUD-07 | P2 | Bekannter PCA-Maßstabsfehler besteht | R/K | GEOM-114 |
| GEO-AUD-08 | P2 | Ray/Sphere setzt normierte Richtungen voraus | R/S | Öffentliche API; kein Produktionscaller gefunden |
| GEO-AUD-09 | P2 | Subviews verwechseln absolute Handles und relative Größen | R/S | Kein Produktions-CreateView gefunden |
| GEO-AUD-10 | P2 | SparseGrid prüft seine Kapazitätsgrenze nicht | R | Randfall großer logischer Dimensionen |
| GEO-AUD-11 | P2 | Mesh-Konvertierung übersieht Attributverluste | S | Kein Produktionscaller gefunden |
| GEO-AUD-12 | P3 | KDE-Rechenkern ist dupliziert | S | PointCloud.Utils |
| GEO-AUD-13 | P3 | 13 Modulnamen widersprechen Dateinamen | S/K | GEOM-115 |
| GEO-AUD-14 | P3 | Physics nutzt vorhandenen Scratch-Overload nicht | S | Physics abgleichen |
| GEO-AUD-15 | P3 | SparseGrid-Freelist wird nie befüllt | S | Kleiner Löschkandidat |

## GEO-AUD-01 PointCloud hält zerstörte Attributspeicher für gültig

- **Ort:** `src/geometry/Geometry.PointCloud.cpp:74–114,178–202`; Speicherfreigabe in `src/geometry/Geometry.Properties.cpp:149–156,255–260`.
- **Repro:** `EnableNormals(); AddPoint(...); Clear();` lässt `HasNormals()` wahr, obwohl `PointProperties().Exists("p:normal")` falsch ist. Zuweisung einer Cloud ohne Normalen in ein bereits attributiertes Ziel reproduziert denselben Widerspruch.
- **Mechanismus/Folge:** `EnsureProperties` löst optionale Handles nicht durchgängig neu auf. `EnableNormals/Colors/Radii` überspringen das Binden wegen alter nichtnulliger Rawpointer. Folgezugriffe können freigegebenen oder wiederverwendeten Speicher lesen/schreiben. Reproduziert wurde die Handle-/Storage-Invariante, kein tatsächlicher UAF oder Sanitizerfehler.
- **Kleinster Fix:** Nach Speicherersetzung optionale Handles aus dem aktuellen Registry neu binden oder invalidieren. Den kommentierten Clear-Vertrag „keep property slots alive“ mit dem tatsächlichen Verhalten abstimmen.
- **Regression:** Clear plus Wiederbenutzung, Copy-/Move-Zuweisung mit vorhandenen/fehlenden Normalen, Farben und Radien in `tests/unit/geometry/Test_PointCloud.cpp`. Fix im Container, nicht in einzelnen Writern.
- **Owner:** Kein passender offener Fix-Owner identifiziert.

## GEO-AUD-02 Graph Export zerstört das Ziel vor vollständiger Validierung

- **Ort:** `src/geometry/Geometry.Graph.IO.cpp:318,340,442,479–482`.
- **Repro:** Vorhandene Datei mit `existing document` durch einen Graphen mit NaN-Position überschreiben: `WriteTGF` meldet Fehler, Datei bereits leer. Analoger EdgeList-/Gewichtspfad nur quellengeprüft; spätere Fehler können Teil-Dateien hinterlassen.
- **Kleinster Fix:** Alle betreffenden Eingaben vor dem Öffnen mit `std::ios::trunc` validieren. Das repariert den nachgewiesenen Eingabefehlerpfad, verspricht aber noch keine atomare Speicherung bei allen IO-Fehlern.
- **Regression:** Bestehende Negativfälle in `tests/unit/geometry/Test.GeometryIO.cpp:2347–2374` mit nichtleerer Sentinel-Zieldatei ergänzen; unveränderten Inhalt bei Fehler verlangen.
- **Owner/Abgleich:** Kein passender offener Fix-Owner identifiziert; mit Assets-/Export- und Core-Dateispeicherbefunden nach konkretem Pfad deduplizieren.

## GEO-AUD-03 Boxkontakte liefern eine konstante falsche Eindringtiefe

- **Ort:** `src/geometry/Geometry.ContactManifold.cppm:139–150`; `Geometry.GJK.cpp:38–48`; `Geometry.EPA.cppm:109` (jeweils unter `src/geometry/`). Verbraucher: `src/physics/Physics.World.cpp:176,253,957`, `Physics.World.cppm:173`.
- **Repro:** Zwei OBBs mit Identitätsrotation, Extents `(1,1,1)`, Zentren `(0,0,0)`/`(1.5,0,0)`: tatsächliche Tiefe `0.5`, zurückgegeben `0.001`.
- **Mechanismus/Folge:** GJK erkennt den Ursprung auf einem Linien-Simplex; EPA verlangt Größe 4. Der Fallback setzt `0.001`, gleich der Standard-Slop-Toleranz des World-Solvers. Der gelesene Solverpfad ergibt damit keine Positionskorrektur. Kein zusätzlicher World-/Sandbox-Lauf.
- **Kleinster Fix:** GJK→EPA-Übergang für treffende Linien-/Dreieck-Simplexe vervollständigen; numerisches Scheitern ehrlich behandeln. Änderung der Konstante allein repariert den Vertrag nicht.
- **Regression:** Analytisch bekannte Tiefe dieses Boxpaars, danach World-Test mit tatsächlicher Positionskorrektur. Treffer-/Normalen-/`depth>0`-Prüfungen reichen nicht.
- **Owner/Abgleich:** Kein passender offener Fix-Owner identifiziert. `PHYSICS-008` besitzt Broadphase, nicht diesen Tiefenfehler. Mit Physikbericht zusammenführen.

## GEO-AUD-04 K Means meldet falsche Konvergenz und inkonsistente Enddiagnostik

- **Ort:** `src/geometry/Geometry.KMeans.cpp:240,283,288–315`; öffentlicher Seed-Vertrag `src/geometry/Geometry.KMeans.cppm:87–100`.
- **Repro:** Punkte `x={0,10,11}`, Startzentren `{0,100}`, `k=2`: `Converged=true`, eine Iteration, Labels `{0,0,0}`, Zentren `{7,11}`. Labels passen nicht zu den Zentren. Inertia `221`; für zurückgegebene Labels/Zentren neu berechnet `74`.
- **Mechanismus:** Erstzuordnung wird gegen künstliche Null-Labels als stabil bewertet. Distanzen/Inertia stammen außerdem vom Zustand vor dem letzten Zentren-Update; der zweite Fehler kann auch beim Iterationslimit auftreten.
- **Kleinster Fix:** Erstiteration/Konvergenzkriterium korrigieren; finale Labels, Distanzen, MaxDistanceIndex und Inertia auf denselben Zentrenzustand beziehen.
- **Regression:** Seed-Fall und `MaxIterations=1`; `PointCloud_KMeans` in `tests/unit/geometry/Test_PointCloud.cpp`; Runtime-Clustering-Publikation. GPU-Semantik bei Reparatur separat abgleichen, hier nicht ausgeführt.
- **Assertion:** Nach Gegenreview präzisiert zu `result && (!result->Converged || assignments)`; nur behauptete Konvergenz fordert konsistente Zuordnung. Auch diese Assertion schlägt fehl.
- **Owner:** Kein passender offener Fix-Owner identifiziert; der Kontrollfluss wurde zusätzlich zu den Repros quellengeprüft.

## GEO-AUD-05 Gelöschte Punkte bleiben in Export und Statistik wirksam

- **Ort:** `src/geometry/Geometry.PointCloud.IO.cpp:1881,2019,2137,2216`; `Geometry.PointCloud.Utils.cpp:190–192,1157–1191`; `Geometry.PointCloud.cppm:60–69,121` (alle unter `src/geometry/`).
- **Repro Export:** Zwei Punkte anlegen, einen löschen, XYZ schreiben/laden: beide kommen zurück. PLY ASCII/Binary und PCD ASCII/Binary verwenden ebenfalls Slots; diese vier Pfade wurden quellengeprüft.
- **Repro Statistik:** Lebende Punkte `x={0,2}`, gelöschter Punkt `100`: Count `3`, Schwerpunkt `34`, mittlerer Abstand `34`; für die lebende Menge richtig: `2`, `1`, `2`.
- **Folge:** `ApplyGaussianNoise` validiert/verändert `LivePoints()`, bestimmt seine Skala aber aus dieser Statistik: hier eine 17-fach zu große Skala. Gelöschte nichtendliche Werte können die Skalenermittlung stören. Diese Noise-Folge ist aus dem Caller hergeleitet, nicht separat als Noise-Lauf ausgeführt. Kein behaupteter Verstoß gegen `Cloud::IsEmpty()`, das aktuell Slots bezeichnet.
- **Kleinster Fix:** Cloud-Adapter und Writer auf konsistente lebende Mengen bringen; generische Span-Kerne behalten. Headeranzahl und sämtliche Exportattribute müssen dieselbe Auswahl verwenden.
- **Regression:** Deleted-Slot-Roundtrips aller fünf Writer mit Attributen und all-deleted-Fall; Statistik und Noise-Skala in `Test.GeometryIO.cpp`/`Test_PointCloud.cpp`.
- **Owner:** Kein passender offener Fix-Owner identifiziert, auch nicht für Statistik/Noise. [GEOM-073](../../tasks/backlog/geometry/GEOM-073-point-analysis-property-span-contracts.md) ist ein benachbarter Adapter-Seam, schließt aber Numerik-/Policy-Änderungen aus und verlangt, bestehendes Deleted-Slot-Verhalten zu erhalten. Die semantische Korrektur dieses Befunds ist daher nicht durch seinen aktuellen Scope abgedeckt; vor Umsetzung eigenen Owner festlegen oder den Task-Scope ausdrücklich ändern.

## GEO-AUD-06 Kleine wohlgeformte Dreiecke werden zu Kanten reduziert

- **Ort:** `src/geometry/Geometry.Triangle.cpp:91,160–177`; `Geometry.MeshClosestFace.cpp:43–52`; Verbraucher `Geometry.HalfedgeMesh.AdaptiveRemeshing.cpp:35`, `Geometry.ImplicitPlaneField.cpp:49–81` (alle unter `src/geometry/`).
- **Repro:** Dreieck `(0,0,0),(5e-4,0,0),(0,5e-4,0)`; innerer Punkt `(1e-4,1e-4,0)` wird auf `(1e-4,0,0)` projiziert. Absoluter Test `|cross|² <= 1e-12` verwirft die Fläche.
- **Reichweite:** Normalen- und MeshClosestFace-Filter sowie deren Verbraucher wurden quellengeprüft; zusätzlich ausgeführt wurde die ClosestPoint-Abfrage.
- **Kleinster Fix:** Gemeinsamen skalenbezogenen Degenerationscheck und konsistente Normalen-/Projektionsrechnung verwenden; nicht nur den Indexfilter lockern.
- **Regression:** Skalierte äquivalente Dreiecke, innerer Punkt, Normalen, Indexaufbau. Der Brute-force-Oracle in `tests/unit/geometry/Test_MeshClosestFace.cpp:80` überspringt selbst kleine Flächen; diesen blinden Fleck mitprüfen.
- **Owner:** Kein passender offener Fix-Owner identifiziert; `GEOM-067` betrifft BVH-Layout-/Speicherevidenz, nicht diesen Maßstabsfehler.

## GEO-AUD-07 Bekannter PCA Maßstabsfehler besteht weiter

- **Ort:** `src/geometry/Geometry.PCA.cpp:162–170`; CPU-/GLSL-Zwilling `assets/shaders/include/pca_eigen_double.glsl`.
- **Repro:** Neun Punkte `(0,i*1e-5,j*1e-5)`, `i,j in {-1,0,1}`: kleinste Eigenrichtung Z statt X. Die bei 1 gekappte Kovarianzskala lässt den isotropen Fallback zu früh greifen.
- **Fix/Regression:** Vorhandenen relativen Cutoff-Task umsetzen; Nullkovarianz, wiederholte Eigenwerte, rotierte Ebenen, Maßstäbe und CPU-/Shader-Konsistenz abdecken.
- **Betroffene:** Normalen, Features, Graph-Normalen, OneRing-PCA, Plane-/Segment-/OBB-Fitting, direkte `SymmetricEigen3`-Nutzer.
- **Owner:** [GEOM-114](../../tasks/backlog/geometry/GEOM-114-scale-relative-pca-isotropic-cutoff.md); nicht duplizieren. GPU-Parität hier nicht geprüft.

## GEO-AUD-08 Ray Sphere setzt normierte Richtungen voraus

- **Ort:** `src/geometry/Geometry.ContactManifold.cpp:99–115`, `Geometry.Overlap.cpp:177–186`; allgemeiner Vertrag `Geometry.Ray.cppm`, `Geometry.Validation.cpp:125–130` (unter `src/geometry/`).
- **Repro:** Ray ab Ursprung, Richtung `(2,0,0)`, Kugelzentrum `(5,0,0)`, Radius 1: erwartet `t=2`, Treffer `x=4`; tatsächlich `t≈1.2822`, `x≈2.5644`. Allgemeiner Ray-Vertrag erlaubt die Richtung.
- **Fix:** `a=dot(d,d)` in Diskriminante `b*b-a*c` und Nullstellen berücksichtigen; ungültige Richtung abweisen. Gemeinsamer Kugelkern für Overlap/RayCast.
- **Regression:** Richtungsfaktoren `0.5,1,2`, identischer Treffpunkt und invers skalierter Parameter; seitlicher Fehlschuss. Overlap-Duplikat bisher nur quellengeprüft.
- **Owner/Exposition:** Kein passender offener Fix-Owner oder Produktionscaller des Kugelpfads gefunden; öffentlicher API-Fehler, kein beobachteter Sandbox-Ausfall.

## GEO-AUD-09 Subviews verwechseln absolute Handles und relative Größen

- **Ort:** `src/geometry/Geometry.PointCloud.cppm:64`; `Geometry.HalfedgeMesh.cppm:132–135`; `Geometry.Mesh.Conversion.cpp:185–208` (unter `src/geometry/`).
- **Repro:** Cloud-View `{Offset=1,Size=1}` lehnt ihren absoluten Handle 1 ab (`1 < 1`).
- **Quellenbefund:** Mesh→Soup beginnt trotz View-Offsets bei Vertex/Face 0; eine View auf das zweite von zwei getrennten Dreiecken kann das erste konvertieren. Dieser Konvertierungsfall wurde nicht ausgeführt.
- **Fix/Regression:** Vertrag absoluter Quellindizes und sichtbarer Bereiche konsistent anwenden; geeignete Live-Ranges/Remaps verwenden. Nichtnullige Offsets bei `IsValid` und Mesh-Konvertierung testen; `tests/unit/geometry/Test.PointCloudConversion.cpp:199,221` hat analoge Cloud-Offsettests.
- **Owner/Exposition:** Kein passender offener Fix-Owner und keine Produktions-`CreateView`-Aufrufe gefunden. Dokumentierte Borrow-Mutationsgrenzen sind keine zusätzlichen neuen Befunde.

## GEO-AUD-10 SparseGrid prüft seine Kapazitätsgrenze nicht

- **Ort:** `src/geometry/Geometry.Grid.cppm:365–370`; `src/geometry/Geometry.Grid.cpp:54,71,91`.
- **Repro:** `NX=1<<24,NY=8,NZ=1`; `TouchVertex(1<<24,0,0)` und `TouchVertex(0,8,0)` liefern denselben Slot. Nur ein kleiner Block wird tatsächlich allokiert.
- **Geltungsgrenze:** Der private Kommentar dokumentiert 21 Bits je Blockachse und bis `2^24` Vertices. `NX=2^24` bedeutet `NX+1` Vertices und überschreitet diese Grenze. Fehler ist die fehlende öffentliche Ablehnung mit stiller Aliasierung, nicht ein Fehler innerhalb des dokumentierten Zahlenbereichs.
- **Fix/Regression:** Dimensionen und direkte Blockzugriffe gegen bestehende Kapazitätsgrenze prüfen; Grenzwerte in `tests/unit/geometry/Test_Grid.cpp`. Kein neuer Grid-Typ nötig.
- **Owner/Exposition:** Kein passender offener Fix-Owner identifiziert. Keine Behauptung, normale Sandbox-Grids erreichten diese Größe.

## GEO-AUD-11 Mesh Konvertierung übersieht bestimmte Attributverluste

- **Ort:** `src/geometry/Geometry.Mesh.Conversion.cpp:251–269`.
- **Quellenfall:** Mesh mit ausschließlich `h:texcoord`, `h:normal` oder `e:feature` wird zur Soup ohne diese Daten; die Verlustprüfung untersucht nur Vertex-/Face-Properties. Kein zusätzlicher Lauf.
- **Fix/Regression:** Verworfene Halfedge-/Edge-Kanäle diagnostizieren; nur diese Kanäle anlegen und ausdrückliche Verlustdiagnose verlangen. Tatsächliches Corner-Remapping ist ein separater größerer Schritt.
- **Owner/Exposition:** Kein passender offener Fix-Owner oder Produktionscaller dieser Konvertierung gefunden; mit Assets-/Export-Bericht auf gemeinsame Consumer abgleichen.

## GEO-AUD-12 KDE Rechenkern ist dupliziert

- **Ort:** `src/geometry/Geometry.PointCloud.Utils.cpp:890,919,1000,1045`; Verbraucher `src/runtime/Editor/Operations/Runtime.PointFieldOperations.Processing.cpp:84`.
- **Problem:** `EstimateKernelDensityFromNeighbors` und `EstimateKernelDensity` unterscheiden sich sinnvoll bei der Nachbarschaftsbeschaffung, duplizieren aber Bandbreitenheuristik, Normalisierung, kleine Exponentialwerte und Ergebnisreduktion. Keine nachgewiesene aktuelle Ergebnisabweichung.
- **Fix:** Privater gemeinsamer Kern mit lokaler Query-Funktion, wie vorhandenes `BilateralStepWithQueries` in derselben Datei. Keine öffentliche Adapterhierarchie.
- **Regression/Owner:** Bestehende Vergleiche in `tests/unit/geometry/Test.KernelDensity.cpp:41` erhalten; kein zusätzlicher Cleanup-Owner identifiziert.

## GEO-AUD-13 Dreizehn Modulnamen passen nicht zu Dateinamen

- **Beleg:** Verstoß gegen vorhandene [API-Stilkonvention](../architecture/geometry-api-style.md), bereits erfasst.
- **Betroffen:** `Geometry.HalfedgeMesh.{Boolean,CatmullClark,Curvature,DEC,Geodesic,ModalAnalysis,Parameterization,Remeshing,SignedHeatMethod,Simplification,Smoothing,Subdivision,VectorHeatMethod}.cppm` exportieren die kürzeren `Geometry.<Name>`-Module.
- **Folge/Fix:** Importnamen und Dateisuche passen nicht zusammen. Die im Task offene Wahl entscheiden; Option A (Modulnamen an Dateien anpassen) vermeidet 26 Dateiumzüge, ändert laut Task aber rund 66 Importstellen. Empfehlung, keine bereits getroffene Betreiberentscheidung. Mechanik und Semantik getrennt halten.
- **Owner:** [GEOM-115](../../tasks/backlog/geometry/GEOM-115-halfedge-module-names-match-file-stems.md); kein zweiter Rename-Task oder Compatibility-Wrapper.

## GEO-AUD-14 Physics nutzt den vorhandenen Scratch Overload nicht

- **Ort:** `src/physics/Physics.World.cpp:176,635–646`; `src/geometry/Geometry.ContactManifold.cppm:155–160`; Heapallokation `src/core/Core.Memory.LinearArena.cpp:43–59`.
- **Fall:** Fallback legt pro Box-/Kapselpaar eine neue 64-KiB-Arena an, vor GJK und damit auch bei getrennten Formen. Die All-Pairs-Schleife hat bei 100 Boxen rechnerisch 4.950 Paare pro Kontaktberechnung; kein gemessener Benchmarkwert.
- **Fix/Regression:** Arena pro Kontaktberechnung, Reset zwischen Paaren, vorhandenen Scratch-Overload nutzen; Kontaktgleichheit/Reset prüfen. Keine gemessene Beschleunigung behauptet.
- **Owner:** Mit Physikbericht deduplizieren. [PHYSICS-008](../../tasks/backlog/physics/PHYSICS-008-spatial-broadphase-and-kdop-deformable-collision.md) besitzt Broadphase; Arena-Wiederverwendung ist davon unabhängig.

## GEO-AUD-15 SparseGrid Freelist wird nie befüllt

- **Ort:** `src/geometry/Geometry.Grid.cpp:105` und `Geometry.Grid.cppm` (`m_FreeBlocks`).
- **Problem/Fix:** Repositoryweite Referenzsuche: Liste wird geleert/abgefragt, nie befüllt. Member und wirkungsloser Wiederverwendungszweig können entfallen, ungefähr zehn Zeilen, keine Abhängigkeit. Keine Block-Recycling-Funktion allein zur Rechtfertigung dieses Codes ergänzen.

## Organisation und Stil

Grundform: spezifische Module, Parameter-/Ergebnisrecords, freie Funktionen,
getrennte Implementierungen, gemeinsame Property-Speicher. Die Importinventur
fand innerhalb Geometry nur `Geometry.*` und `Extrinsic.Core.*`; der ausgeführte
strikte Layering-Check meldete keine Verstöße. Große Interfaces wie
`Geometry.Properties.cppm` enthalten überwiegend notwendige Templates, Typen
und Dokumentation. Dateilänge allein rechtfertigt keinen Split.

Relevante Unterschiede sind semantisch: Handle-Lebensdauer, Slots/lebende
Elemente, absolute/relative Handles, Maßstabsgrenzen, Ersatzwerte bei Fehlern.
Die äußere Form ist überwiegend einheitlich, die semantischen Verträge sind es nicht.
Ältere `bool`-/`optional`-APIs und neuere Status-/Diagnosestrukturen koexistieren.
Die Stilpolicy erlaubt ältere Formen bis zu einer gezielten Migration; daraus
keine pauschale Vertragsverletzung ableiten.

Keine neue Geometry-Umbrella, Service-/Manager-Schicht oder allgemeine
Algorithmushierarchie einführen. KDTree, BVH, PointLBVH und MeshClosestFace
haben unterschiedliche Element-/Distanzsemantik; ähnliche Traversierung reicht
nicht zur Zusammenlegung. Bei anstehenden Änderungen können Algorithmen aus
`PointCloud.Utils` private Implementierungsdateien erhalten; öffentliche Module
zunächst behalten. Der erste konkrete Gewinn wäre GEO-AUD-12.

## Bekannte Grenzen und vorhandene Aufgaben

| Thema | Vertrag oder Owner | Einordnung |
| --- | --- | --- |
| PCA-Skala | GEOM-114 / GEO-AUD-07 | Bekannter Defekt, reproduziert |
| Modulnamen | GEOM-115 / GEO-AUD-13 | Betreiberentscheidung noch offen |
| Point-Analysis-Adapter | [GEOM-073](../../tasks/backlog/geometry/GEOM-073-point-analysis-property-span-contracts.md) | Teilweise geliefert; verbleibende Bounds-/Noise-Seams |
| Geborgte Graph-/Adjazenzverträge | [GEOM-074](../../tasks/backlog/geometry/GEOM-074-graph-property-adjacency-contracts.md) | Vorhandene Aufgabe, kein neues Graph-Framework planen |
| Partiell überlappende Mesh-CSG | `src/geometry/Geometry.HalfedgeMesh.Boolean.cppm:38–49` | Explizit nicht unterstützt, `Compute` liefert `nullopt` |
| Exact/adaptive Prädikate, incircle/insphere | [GEOM-007 Historie](../../tasks/archive/GEOM-007-robust-predicates-intersection-classification.md), [Numeric Policy](../architecture/geometry-api-style.md) | Foundation vorhanden; Erweiterungen nicht als geliefert ausweisen, aktuelle Owner vor Planung auflösen |
| Simplification-Varianten C/D | `src/geometry/Geometry.HalfedgeMesh.Simplification.cppm:120–121` | Explizit nicht implementiert, ohne konkreten Owner; kein automatisch erforderliches Feature |
| Sandbox-Export-Integration | [UI-046](../../tasks/done/UI-046-sandbox-geometry-export.md) | Runtime/UI-Aufgabe, kein Anlass für einen weiteren Geometry-Writer |

Kein zusätzlicher Bugbefund: Runtime-Subdivision meldet UV-Verlust ausdrücklich
als `TexcoordOutcome::Discarded` samt Erklärung. Daraus keinen stillen Verlust
behaupten. Bekannte Borrow-Mutationsgrenzen sind dokumentiert. Vollständige
Framework24-Feature-Parität wurde hier nicht auditiert.

## Ausgeführte Prüfung und Reproduktion

Ergebnisse aus dem vorangegangenen Audit in derselben Unterhaltung am
2026-10-16, keine neue Vollprüfung beim Speichern dieses Berichts:

```bash
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicGeometryIoTests -j 4
ctest --test-dir build/ci --output-on-failure --timeout 60 \
  -R '^(PointCloud_Cloud|PointCloud_KMeans|GeometryIO_PointCloudIO|GeometryIO_GraphIO|PcaPrimitives|TriangleMetrics|GeometryMeshClosestFace|ContactManifold|Raycast|Overlap|Grid|SparseGrid)\.'
python3 tools/repo/check_layering.py --root src --strict
```

Build erfolgreich; **205 ausgewählte Tests bestanden**. Layering: 960 Dateien,
10.266 Import-/Include-Referenzen, 89 CMake-Linkkanten, kein Verstoß. Die Regex
ist die tatsächliche Auswahl, keine Zusage, jeder Präfix habe Tests getroffen.

Standalone-Repro gegen unveränderte Bibliotheken: **13 fehlgeschlagene
Vertragsprüfungen, Exitcode 1**, nicht 13 unabhängige Bugs:

| Befund | Assertions | Beobachtung |
| --- | ---: | --- |
| GEO-AUD-01 | 2 | Clear-/Assignment-Handles passen nicht zum Storage |
| GEO-AUD-02 | 1 | TGF-Fehler zerstört Sentinel-Datei |
| GEO-AUD-03 | 1 | Box-Tiefe `0.001` statt `0.5` |
| GEO-AUD-04 | 2 | Falsche Konvergenz; Inertia `221` statt `74` |
| GEO-AUD-05 | 2 | Deleted-XYZ-Roundtrip; Statistik `3/34/34` statt `2/1/2` |
| GEO-AUD-06 | 1 | Innerer Punkt landet auf Dreieckskante |
| GEO-AUD-07 | 1 | Kleine YZ-Ebene erhält Z-Normale |
| GEO-AUD-08 | 1 | Kugeltreffpunkt liegt außerhalb |
| GEO-AUD-09 | 1 | Subview lehnt absoluten Handle ab |
| GEO-AUD-10 | 1 | Verschiedene Koordinaten bekommen denselben Grid-Slot |

Keine Fixes, Mechanismus-Mutationen oder neuen registrierten Regressionstests
implementiert. Fix-Tasks müssen passende Fälle in reguläre Tests übernehmen
und ihren Vorher-/Nachher-Nachweis führen.

### Lokale Originalbelege

Verzeichnis: `tasks/evidence/SRC-GEOMETRY-AUDIT-2026-10-16/`. Gemäß Repository-Regel
ignorierte temporäre Belege, lokal für die Gesamtauswertung verfügbar; nicht
Teil eines normalen Git-Commits. Letzte Consumer sind die ausstehende gemeinsame
Auswertung und daraus entstehende Fix-Aufgaben. Erst danach nach Retention-Regel
aufräumen. Inputs, Erwartungen, Ergebnisse und Quellen oben bleiben ohne Logs verständlich.

| Datei | Inhalt |
| --- | --- |
| `geometry_audit_repro.cpp` | Originaler ausgeführter C++-Repro |
| `geometry-audit-repro.log` | Finale Ausgabe der 13 fehlschlagenden Assertions |
| `geometry-build.log` | Vollständige Build-Ausgabe |
| `geometry-tests.log` | Vollständige Ausgabe der 205 CTests |
| `verification-summary.txt` | Ursprüngliche kompakte Prüfzusammenfassung |
| `review-record.json` | Rollen, Gegencheck und Präzisierungen |
| `sha256.json` | Hashes der übernommenen Originalbelege |

SHA-256 des final ausgeführten Repro-Quelltexts:
`81f362cd7206b9a23dd75af9cea53a5cf4dd3656d26d36974b503f0f355233ec`.

Kompilierungsrezept, aus `build/ci`:

```bash
/bin/clang++-23 -std=c++23 -g -fno-exceptions -fno-rtti \
  -DGLM_FORCE_DEPTH_ZERO_TO_ONE -DGLM_FORCE_RADIANS -DGLM_FORCE_XYZW_ONLY \
  -DGLM_RIGHT_HANDED -DEIGEN_MPL2_ONLY \
  -isystem ../../external/vcpkg-installed/ci/x64-linux/include \
  -isystem ../../external/vcpkg-installed/ci/x64-linux/include/eigen3 \
  -fprebuilt-module-path=src/geometry/CMakeFiles/IntrinsicGeometry.dir \
  -fprebuilt-module-path=src/core/CMakeFiles/ExtrinsicCore.dir \
  ../../tasks/evidence/SRC-GEOMETRY-AUDIT-2026-10-16/geometry_audit_repro.cpp \
  lib/libIntrinsicGeometry.a lib/libExtrinsicCore.a \
  ../../external/vcpkg-installed/ci/x64-linux/debug/lib/libxatlas.a \
  ../../external/vcpkg-installed/ci/x64-linux/debug/lib/libglm.a \
  -o ../../tasks/evidence/SRC-GEOMETRY-AUDIT-2026-10-16/geometry_audit_repro
```

Nur die ursprünglichen absoluten Quell-/Bibliotheks-/Outputpfade wurden hier
auf den Repository-Belegordner relativiert; diese Variante wurde beim
Dokumentieren nicht nochmals ausgeführt. Compiler/Modulflags müssen zum
vorhandenen `ci`-Build passen. Der Originalquelltext enthält noch absolute
Fixture-Pfade unter dem Codex-Chat-`work/`-Verzeichnis; vor portabler Ausführung
auf ein frisches temporäres Verzeichnis setzen. Konkret stehen in Quellzeilen
51–52 und 56 `deleted.xyz` und `preserve.tgf` unter
`/home/alex/Documents/Codex/2026-10-16/bitte-schaue-dir-src-assets-genau-2/work/`.
Vor Ausführung Existenz und Schreibbarkeit dieses oder des ersetzten Verzeichnisses
prüfen. Fehlt es, können die XYZ-/TGF-Sammelassertionen aus dem falschen Grund
scheitern; allein Exitcode 1 belegt dann diese beiden Defekte nicht. Beim
ursprünglichen Lauf existierte das Verzeichnis. Der TGF-Test überschreibt
absichtlich seine eigene Sentinel-Datei, niemals ein Nutzerdokument verwenden.

## CI Ausgangslage

- [ci-linux-clang 38869189381](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38869189381), gleicher Commit: `Capture CPU test selection` scheitert, weil `RenderWorldPoolEngineWiring.SourceRules` keinem registrierten Producer zugeordnet wird. Fehlender Timing-Report ist eine Folge; Full-CPU-Testschritt lief dort nicht. Diagnose im ursprünglichen Audit direkt über `gh run view 38869189381 --log-failed` gelesen. Owner: [BUG-244](../../tasks/backlog/bugs/BUG-244-cpu-selection-rejects-source-rules-producer.md).
- [nightly-deep 38854737500](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38854737500), Commit `4a5eede778b0830787fa139156f468c7ecf553e4`: Benchmark-/Sealing-Schritt fehlgeschlagen. [BUG-245](../../tasks/backlog/bugs/BUG-245-nightly-smoke-backend-manifest-mismatch.md) enthält die Detaildiagnose zweier Backend-/Manifest-Namen aus dem parallelen Assets-Audit. Diese Detaildiagnose wurde hier nicht unabhängig aus dem vollständigen Nightly-Log reproduziert.
- `ci-docs` war für den geprüften HEAD erfolgreich. Rote Läufe belegen keine fehlgeschlagenen Geometry-Numeriktests. Keine CI-Reparatur in diesem Audit.

## Übergabe an die gemeinsame src Auswertung

1. Einstieg über die [gemeinsame Sammlung](src-audits.md) und `docs/index.md`; Nachbarberichte liegen in `docs/reviews/` und `docs/reports/`. Anhand Datum und `src-<bereich>-audit` suchen. Keine Vollständigkeit anderer Bereiche aus diesem Bericht ableiten.
2. Revisionen der Berichte vergleichen, aktuelle Quellen/Taskzustände vor Umsetzung erneut lesen. Zeilen beziehen sich auf den oben genannten Commit.
3. Nach Pfad und verletztem Vertrag deduplizieren: GEO-AUD-02/05/11 mit Assets/Export, 03/14 mit Physics, 04/05 mit Runtime, 07 mit Shader-/Graphics-Arbeit. Ein Consumerfehler ist nicht automatisch ein neuer Fehler des darunterliegenden Layers.
4. GEOM-074/114/115 und BUG-244/245 im jeweiligen Scope weiterverwenden. GEOM-073 ist nur der benachbarte Adapter-Seam, kein Owner der semantischen GEO-AUD-05-Korrektur. „Kein Owner identifiziert“ beschreibt die geprüften Aufgaben, nicht einen Beweis, dass nirgendwo verwandte Arbeit existiert.
5. Kleine vollständige Fix-Blöcke mit Regression vor Änderung bilden; Namens-/Dateiumzüge von Semantik trennen. Neue Experimente oder Performancebehauptungen benötigen die vorgeschriebenen eigenen Reviews.

Nicht vollständig geprüft: alle rund 88.000 Zeilen, sämtliche Parser und
Processing-Algorithmen, vollständige CPU-Suite, Sanitizer, GPU, Live-Sandbox,
Performance und vollständige Framework24-Parität. Die konkreten Beispiele
tragen ihre jeweiligen Befunde, keine Behauptung einer erschöpfenden Prüfung.

Bei der lokalen Übergabe sind dieser Bericht, Nachbarberichte und BUG-244/245
noch uncommitted. Vor einer Veröffentlichung aus einem Git-Checkout müssen
verlinkte Dokumente mit übernommen werden; lokal gültige Links garantieren
noch keine Vollständigkeit eines späteren Commits. Die ignorierten Rohbelege
bleiben davon getrennt und sind nicht automatisch auf einem anderen Host vorhanden.
