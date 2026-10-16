# ECS Audit und Übergabe

## Auftrag und ursprüngliche Fragen

1. „sollten wir ecs aufräumen?“
2. „anders organisieren?“
3. „hat die implementierung einen durchgenden stil?“
4. „gibt es offene punkte, bugs, implementierungs gaps?“

Weitere Vorgabe: „lasse subagenten aus verschiedenen "use-case rollen" und
perspektiven darauf schauen.“ Anschließender Auftrag:
„Erstelle einen Bericht aus deinen Ergebnissen in IntrinsicEngine so dass Codex
6 Astra Ultra anschließend alles finden kann. Ich lasse für alle weiteren
unterordner von IntrinsicEngine/src von anderen Agenten ebenfalls so einen
Bericht erstellen“.

Dieser Bericht ist die Übergabe an Codex 6 Astra Ultra und die spätere gemeinsame
Auswertung der [src-Berichte](src-audits.md). Die stehende Repository-Mission ist
Framework24-Konvergenz gemäß [AGENTS.md](../../AGENTS.md). Das ausdrücklich
beauftragte Audit dient einer verlässlichen Szenendatenbasis. Ein separates
Goal-File wurde nicht gefunden; es wurde kein Forschungs- oder Performanceauftrag
aus der Anfrage abgeleitet.

## Ergebnis und Gültigkeit

**Gezielt aufräumen; die bestehende Organisation beibehalten.** Vorrang haben
verletzte Datenverträge und fehlende Randfalltests. Zusätzliche Ordner, eine neue
Registry-Abstraktion oder ein anderes ECS-Framework lösen die belegten Probleme
nicht.

| Merkmal | Stand |
| --- | --- |
| Auditdatum | 2026-10-16, Europe/Berlin |
| Repository | IntrinsicEngine |
| Geprüfter Commit | `66da33fd6bfabb20408607b8361354fc9adb420d` |
| Hauptbereich | `src/ecs`, ECS-Unit-Tests und direkte Runtime-Consumer |
| Umfang | Etwa 2.900 C++-Zeilen unter `src/ecs`; Runtime gezielt an den Nutzungsgrenzen verfolgt |
| Angenommene Nutzung | Interaktiver Editor, verschachtelte Modelle, Geometrieverarbeitung, eine Registry mit einem schreibenden Thread |
| Bestehende Verifikation | Fokussierter Build erfolgreich; 113 ausgewählte CTests grün; Layering-Prüfung grün |
| Zusätzliche Reproduktionen | Sieben Fehlerfälle verletzen ihre Soll-Assertions; zwei Kontrollfälle bestehen |
| Umsetzung | Keine Produktionscodeänderungen durch dieses Audit; keine der beschriebenen Reparaturen umgesetzt |

Zeilenangaben gelten für diesen Commit. Bei späteren Änderungen zusätzlich nach
dem angegebenen Symbol suchen. `ECS-20261016-001` bis `ECS-20261016-014` sind
stabile **Befundkennungen**, keine angelegten Task-IDs oder Implementierungsfreigaben.
Der [ECS-Bericht vom Mai](2026-05-13-src-ecs-gap-analysis.md) bleibt historischer
Kontext; seine damaligen offenen Punkte wurden nicht ungeprüft übernommen.

## Perspektiven und Organisation

| Rolle | Reviewer dieser Sitzung | Schwerpunkt |
| --- | --- | --- |
| Szenen- und Editor-Nutzer | `/root/scene_editor` | Erstellen, Ablösen, Reparenting, Save/Load |
| Runtime-Integrator | `/root/frame_runtime` | Frame-Ablauf, Rendering, Bounds, Physics |
| API- und Geometrie-Wartung | `/root/api_maintainer` | Geometrie-Publikation, Ownership, Consumer, Stil und tote Oberflächen |
| Koordination | Hauptagent | Aufrufer-/Testabgleich, fokussierter Build, deterministische Probes |

Die drei Subagenten arbeiteten zuerst unabhängig, lesend und ohne weitere
Delegation; diese Begrenzung war instruktionell. Danach wurden ihre Urteile mit
den tatsächlichen Probes abgeglichen. Verwendet wurden Ponytail und Ponytail
Audit sowie die einschlägigen Repository-Regeln.

Die gemeinsame Grundform ist brauchbar: einfache CPU-Komponenten, freie
Funktionen, überwiegend getrennte Moduloberflächen und Implementierungen,
explizite FrameGraph-Reihenfolge und klare Ownership. TransformHierarchy →
BoundsPropagation → RenderSync ist auch im tatsächlichen Runtime-Flush
verdrahtet. Geometriequellen besitzen ihre Daten; Provenienz und tatsächlich
vorhandene Domänen werden getrennt abgefragt.

Beibehalten werden sollten `Components/`, `Hierarchy/`, `Systems/` und die
kleinen Scene-Module, die dünne Registry-Hülle mit explizitem `Raw()`, die
Trennung von Strukturprimitiven und Transformationsmutationen sowie die
Runtime-eigenen Physics-/Graphics-Sidecars. Guarded Hierarchieabfragen mit
expliziten Fehlerstatus und ohne Teilergebnisse sind ebenfalls sinnvoll.

Nicht durchgehend sind die Modulnamen (`Component`/`Components`, `Light`/`Lights`,
`ShadowCaster`/`Shadows`, `Transform.Local` als Dateiname gegenüber `Transform`
als Modul) und die Fehlersemantik. Queries liefern Statuswerte, Mutationen
können still scheitern, Runtime ergänzt teilweise Validierung am Consumer.
Gemeinsame Funktionsverträge sind wichtiger als eine Umbenennungskampagne.

Bewusste Entscheidungen sind keine neuen Bugs: `Registry::Destroy` ist ein
Einzelentitätsprimitiv; rekursives Löschen und Undo gehören in Runtime/Editor.
Optionale StableIds mit Runtime-Lookup, rohe Asset-IDs innerhalb der bestehenden
Layergrenze und aus ECS entfernte Legacy-Funktionen sind beabsichtigt.

## Befundübersicht

`R`: im echten gebauten Modul reproduziert. `S`: aus Code und Aufrufern
hergeleitet, ohne ausgeführten vollständigen Runtime-Repro. `D`: statisch
belegte Dokumentations- oder API-Bereinigung. Prioritäten sind Empfehlungen.

| ID | Priorität | Evidenz | Zuständigkeit | Befund |
| --- | --- | --- | --- | --- |
| ECS-20261016-001 | P1 | R | ecs/Hierarchy | Detach lässt lokale und globale Pose widersprüchlich |
| ECS-20261016-002 | P2 | R | ecs/Hierarchy und Transform | Reparenting verwirft erforderliche Scherung |
| ECS-20261016-003 | P2 | R | ecs/Transform | Unendliche Translation wird akzeptiert |
| ECS-20261016-004 | P1 | R | ecs/GeometrySourcesPopulate | Cloud-Property-Typkonflikt verursacht Abbruch |
| ECS-20261016-005 | P2 | R + S | ecs/Population, runtime/GeometryIntegration | Cloud-Löschmaske erreicht Consumer unter falschem Namen |
| ECS-20261016-006 | P2 | R | ecs/GeometrySourcesPopulate | Cloud-Teilansicht erzeugt widersprüchliche Zeilenanzahlen |
| ECS-20261016-007 | P2 | R | ecs/BoundsPropagation | Gespiegelte Geometrie liegt außerhalb ihrer OBB |
| ECS-20261016-008 | P2 | S | runtime/Rendering | OBB-Rotation fehlt bei AABB-Extraktion |
| ECS-20261016-009 | P1 | S | runtime/Scene | Abgewiesene Elternbeziehungen gelten beim Laden als Erfolg |
| ECS-20261016-010 | P2 | S | runtime/Scene | Geschwisterreihenfolge geht bei Save/Load verloren |
| ECS-20261016-011 | P2 | S | runtime/PhysicsIntegration | Lokale ECS-Pose wird als Physics-Weltpose verwendet |
| ECS-20261016-012 | P2 | S | runtime/PhysicsIntegration | Nicht unterstützte Authoring-Felder bleiben still wirkungslos |
| ECS-20261016-013 | P3 | D | ecs/Dokumentation | Falsche Imports, Ownership- und Statusbeschreibung |
| ECS-20261016-014 | P3 | D | ecs/Selection | PickID ist eine ungenutzte zweite ID-Oberfläche |

## Reproduzierte ECS Befunde

### ECS-20261016-001 Detach und Transformation

**Fundstelle:** [ECS.Hierarchy.Mutation.cpp](../../src/ecs/Hierarchy/ECS.Hierarchy.Mutation.cpp),
Zeilen 18–26, `Detach`. Reeller Consumer beim Modellabbau:
[Runtime.AssetWorkflowModelMaterialization.cpp](../../src/runtime/AssetWorkflow/Runtime.AssetWorkflowModelMaterialization.cpp),
Zeilen 688–703.

**Fall und Beobachtung:** Parent bei X=10, Kind lokal X=2, nach Systemlauf global
X=12. Detach entfernt die Verbindung, lässt lokal X=2 und global X=12 ohne
Dirty-Tag stehen. Der nächste Systemlauf erhält die Inkonsistenz.

**Folge und kleinster Fix:** Das weiterlebende Kind kann beim nächsten Edit oder
nach Save/Load springen. Beim Detach Root-Pose konsistent herstellen und Dirty
markieren; nicht darstellbare Zustände vor Änderung der Verbindungen behandeln.
Die Inkonsistenz ist unabhängig von der gewählten Pose-Erhaltungspolitik ein Fehler.

**Regression:** [Test.ECS.Hierarchy.cpp](../../tests/unit/ecs/Test.ECS.Hierarchy.cpp)
prüft Detach bisher nur strukturell. Parent/Kind-Translation, Systemlauf vor/nach
Detach und anschließende Bearbeitung ergänzen. Probe: `detach`.

### ECS-20261016-002 Reparenting mit Scherung

**Fundstelle:** [ECS.Hierarchy.Mutation.cpp](../../src/ecs/Hierarchy/ECS.Hierarchy.Mutation.cpp),
Zeilen 50–75, `Attach`; [ECS.Component.Transform.Local.cpp](../../src/ecs/Components/ECS.Component.Transform.Local.cpp),
Zeilen 22–49, `TryDecomposeMatrix`.

**Fall und Beobachtung:** Sauberes Kind mit Weltrotation Rz(45°), neuer Parent
mit Skalierung (2,1,1). Die erforderliche lokale Matrix enthält Scherung, die
das TRS-Component nicht speichern kann. Nach Attach und Systemlauf beträgt die
maximale Differenz eines Weltmatrixeintrags im Repro 0,424264 statt weniger als
1e-5. Das verletzt den dokumentierten Pose-Erhalt bei bereiten Endpunkten.

**Folge und kleinster Fix:** Die sichtbare Pose ändert sich beim Reparenting.
Kandidaten vor Linkmutation berechnen und ihre TRS-Rekonstruktion gegen die
gewünschte lokale Matrix prüfen; nicht darstellbare Kandidaten ablehnen. Das
Vorbild existiert in [Runtime.GizmoInteraction.cpp](../../src/runtime/Gizmos/Runtime.GizmoInteraction.cpp),
Zeilen 596–604, `MatchesTrs`. Kein neues Transformationsformat nötig; ein
Identitäts-Fallback löst diesen Fehler nicht. Die ausdrücklich dokumentierte
Singularitäts-Policy ist ein anderer Fall.

**Regression:** Rotation unter nichtuniformer Skalierung in
`Test.ECS.Hierarchy.cpp`; bei Ablehnung müssen Pose und Links unverändert bleiben.
Probe: `reparent_shear`.

### ECS-20261016-003 Nicht endliche Translation

**Fundstelle:** [ECS.Component.Transform.Local.cpp](../../src/ecs/Components/ECS.Component.Transform.Local.cpp),
Zeilen 35–49, `TryDecomposeMatrix`.

**Fall und Beobachtung:** Identitätsmatrix mit `matrix[3].x = infinity`.
Der Helfer liefert `true` und schreibt `Position.x = infinity`.

**Folge und kleinster Fix:** Der gemeinsame Validierungspfad akzeptiert
unbrauchbare Daten. Position und relevanten Matrix-/TRS-Vertrag vor Ausgabe
vollständig prüfen; Ausgabe bei Fehler unverändert lassen. Gizmo und Modellimport
besitzen Zusatzprüfungen, daher ist hier kein Editor-Absturz nachgewiesen.

**Regression:** Direkter Helfertest für Inf/NaN-Translation und unveränderte
Ausgabe bei Fehler. Probe: `nonfinite_transform`.

### ECS-20261016-004 Typkonflikt beim Cloud Publish

**Fundstelle:** [ECS.Component.GeometrySourcesPopulate.cpp](../../src/ecs/Components/ECS.Component.GeometrySourcesPopulate.cpp),
Zeilen 244–268, `PopulateFromCloud`. Abbruch in
[Geometry.Properties.cppm](../../src/geometry/Geometry.Properties.cppm), Zeile 413,
`PropertyBuffer<T>::Vector`.

**Fall und Beobachtung:** Cloud mit normalen Positionen und zusätzlichem
`GetOrAddVertexProperty<float>("v:position", 1.f)` publizieren. Der kopierte Name
kollidiert mit dem erwarteten `glm::vec3`; der ungültige Handle assertiert beim
`Vector()`-Zugriff. Der analoge `v:normal`-Pfad wurde nicht separat ausgeführt.

**Erreichbarkeit:** Über die öffentliche Cloud-API reproduziert. Im Audit wurde
kein aktueller Import-/Editor-Ablauf nachgewiesen, der genau diesen
Namens-/Typkonflikt erzeugt. P1 bewertet den Abbruch bei diesem API-Eingang,
nicht einen belegten Absturz im normalen Importablauf.

**Folge und kleinster Fix:** Ein erlaubter benutzerdefinierter Property-Name
kann die Population abbrechen. Die vorhandene Funktion `RemovePropertyIfPresent`
wie bei Mesh und Graph vor dem Schreiben nutzen; kein neuer Adapter nötig.

**Regression:** [Test.ECS.GeometrySourcesPopulate.cpp](../../tests/unit/ecs/Test.ECS.GeometrySourcesPopulate.cpp)
um Konflikte bei Position und aktivierten Normalen ergänzen. Probe: `cloud_conflict`.

### ECS-20261016-005 Cloud Löschmaske

**Fundstelle:** `PopulateFromCloud`, Zeilen 250–260 derselben Datei. Consumer:
[Runtime.SpatialIndexCache.cpp](../../src/runtime/GeometryIntegration/Runtime.SpatialIndexCache.cpp),
Zeilen 88–103 und 784–788; [Runtime.RenderExtraction.VectorFields.cpp](../../src/runtime/Rendering/Runtime.RenderExtraction.VectorFields.cpp),
Zeilen 242–246.

**Fall und Beobachtung:** Drei Cloud-Punkte, mittleren mit `DeletePoint` löschen,
dann publizieren. ECS enthält drei Positionen und `NumDeleted=1`, besitzt
`p:deleted`, aber kein `v:deleted`. Dieser Teil ist reproduziert. Die genannten
Runtime-Consumer lesen für Cloud-Punkte `v:deleted`; ohne Maske behandeln sie
die Zeilen als lebend. Diese Folge ist aus dem Code hergeleitet, ohne
ausgeführten SpatialIndex-/Rendering-Durchlauf.

**Erreichbarkeit:** Im Produktionscode unter `src` wurde kein Aufruf von
`Cloud::DeletePoint` gefunden. Die gelöschte Cloud wurde über die öffentliche
API für den Repro erstellt; die Consumer-Folge wird relevant, sobald eine solche
Cloud publiziert wird. Ein derzeit aktiver Editor-Löschfehler ist nicht belegt.

**Folge und kleinster Fix:** Gelöschte Punkte können räumliche Abfragen und
Overlays beeinflussen. Cloud-Löschmaske beim Publizieren konsistent in den
kanonischen ECS-Kanal übertragen; Counts, Zeilen und Maske gemeinsam prüfen.

**Regression:** Populationstest plus Integration in
[Test.SpatialIndexCache.cpp](../../tests/contract/runtime/Test.SpatialIndexCache.cpp).
Der vorhandene Fixture-Aufbau schreibt `v:deleted` direkt und umgeht die Naht.
Probe: `cloud_deleted`.

### ECS-20261016-006 Cloud Teilansichten

**Fundstelle:** `PopulateFromCloud`, Zeilen 253–260. `Cloud::CreateView` in
[Geometry.PointCloud.cpp](../../src/geometry/Geometry.PointCloud.cpp), Zeile 46 ff.,
begrenzt `Positions()`, während `PointProperties()` die Basisspeicherung zeigt.

**Fall und Beobachtung:** Drei Basispunkte und
`Cloud::CreateView(cloud, Geometry::ElementRange{1u,1u})`. Nach Population hat
`Properties.Size()` den Wert 3, `v:position.Size()` jedoch 1.

**Folge und kleinster Fix:** Counts und Attributzeilen widersprechen sich.
Teilansichten für alle Properties konsistent materialisieren oder vor jeder
Registry-Mutation ausdrücklich zurückweisen; ein Debug-Assert genügt nicht.
Die API dokumentiert keine Beschränkung auf volle Container. Die gelesenen
Runtime-Aufrufer verwenden überwiegend volle Kopien, daher wird kein
Standardimportfehler behauptet.

**Regression:** Teilansicht mit zusätzlichem Benutzerattribut in
`Test.ECS.GeometrySourcesPopulate.cpp`; bei Zurückweisung alte ECS-Daten erhalten.
Probe: `cloud_view`.

### ECS-20261016-007 Gespiegelte und gescherte Bounds

**Fundstelle:** [ECS.System.BoundsPropagation.cpp](../../src/ecs/Systems/ECS.System.BoundsPropagation.cpp),
Zeilen 63–76, `ComputeWorldBounds`.

**Fall und Beobachtung:** Lokale AABB `[-1,1]^3`, Weltmatrix
`Rz(90°) * scale(-2,1,1)`. Eine Ecke liegt in berechneten OBB-Koordinaten bei
(1,2,-1), die Extents sind aber (2,1,1). Der Einschluss-Assert scheitert.
Nichtorthogonale Spalten durch Scherung gehen ebenfalls ungeprüft an
`quat_cast`; dieser zusätzliche Fall ist quellcodegestützt.

**Folge und kleinster Fix:** Die orientierte Box ist kein verlässliches
einschließendes Volumen. Spiegelung vor Quaternion-Konversion behandeln; bei
Scherung die verfügbare transformierte AABB als konservative OBB mit
Identitätsrotation verwenden. GPU-Frustum-Culling verwendet derzeit die
Weltkugel in [instance_cull.comp](../../assets/shaders/instance_cull.comp), daher
ist kein aktueller Sichtbarkeitsfehler nachgewiesen.

**Regression:** [Test.ECS.BoundsPropagation.cpp](../../tests/unit/ecs/Test.ECS.BoundsPropagation.cpp)
prüft beim vorhandenen Scherungstest die Kugel. Bei Spiegelung und Scherung
zusätzlich alle acht transformierten Ecken auf OBB-Einschluss prüfen.
Probe: `bounds_reflection`.

## Quellcodegestützte Runtime Grenzbefunde

Mit den anderen Bereichsberichten nach Ursache und verantwortlichem Consumer
deduplizieren. Ein Runtime-Adapterfehler ist nicht automatisch ein Fehler des
darunterliegenden ECS-/Physics-/Graphics-Subsystems.

### ECS-20261016-008 Rotation bei AABB Extraktion

**Fundstelle:** [Runtime.RenderExtraction.cpp](../../src/runtime/Rendering/Runtime.RenderExtraction.cpp),
Zeilen 429–430 und 480–481, beide Extraktionspfade.

**Fall und Folge:** OBB mit Extents (2,1,1), um 90° um Z gedreht.
`Center ± Extents` ignoriert Rotation und erzeugt falsche GPU-AABB-Grenzen.
Das ist ein falscher Datenvertrag, kein nachgewiesener Ausfall des derzeit
kugelbasierten Frustum-Cullings.

**Kleinster Fix und Test:** Nach Sicherstellung des OBB-Vertrags aus 007 den
vorhandenen Konverter `Geometry::ToAABB` aus
[Geometry.OBB.cpp](../../src/geometry/Geometry.OBB.cpp), Zeile 170, nutzen.
Rotierte ungleichseitige Box in
[Test.RuntimeRenderExtraction.cpp](../../tests/integration/runtime/Test.RuntimeRenderExtraction.cpp)
prüfen; beide Pfade berücksichtigen.

### ECS-20261016-009 Ungültige Hierarchie beim Laden

**Fundstelle:** [Runtime.SceneSerialization.cpp](../../src/runtime/Scene/Runtime.SceneSerialization.cpp),
Zeilen 2880–2890, Elternverknüpfung vor `scene.Clear()`.

**Fall und Folge:** `{"version":5,"entities":[{"id":0,"parentId":0}]}` erfüllt
die vorgelagerten Formatprüfungen. Attach weist Selbstparenting zurück, der
Loader zählt trotzdem einen Link und ersetzt die bisherige Szene. Analog bei
zwei gegenseitigen Elternbeziehungen. Die ECS-Ablehnung wird als Erfolg behandelt.

**Kleinster Fix und Test:** Nach Attach tatsächlichen Parent gegen angeforderten
Parent prüfen; mit `InvalidFormat` vor `scene.Clear()` abbrechen.
[Test.RuntimeSceneSerialization.cpp](../../tests/contract/runtime/Test.RuntimeSceneSerialization.cpp)
um Selbstparent/Zyklus und Erhalt der bisherigen Szene ergänzen. Kein neues
ECS-Result-Framework nötig.

### ECS-20261016-010 Geschwisterreihenfolge bei Save und Load

**Fundstelle:** `Runtime.SceneSerialization.cpp`, Zeilen 2898, 2947–2954 und
2880–2885; [Runtime.SceneDocumentModule.cpp](../../src/runtime/Scene/Runtime.SceneDocumentModule.cpp),
Zeilen 93–120, `CopySerializableHierarchy`.

**Fall und Folge:** Parent, A, B erzeugen; B und dann A anhängen. Ausgangsfolge
A,B. Der Serializer speichert ID-Reihenfolge und nur `parentId`; Laden hängt
A und dann B vorne ein, Ergebnis B,A. Der asynchrone Save-Snapshot kopiert
ebenfalls nur `Parent`; eine reine Serializer-Änderung wäre unvollständig.

**Kleinster Fix und Test:** Reihenfolge durch Snapshot, Format und Aufbau
tragen und Head-Insertion berücksichtigen. Roundtrip mit Geschwisterfolge
ungleich ID-Reihenfolge in `Test.RuntimeSceneSerialization.cpp` und
[Test.SceneDocumentModule.cpp](../../tests/contract/runtime/Test.SceneDocumentModule.cpp).
ECS-Queries garantieren exakte Geschwisterreihenfolge; im Dokumentpfad geht sie verloren.

### ECS-20261016-011 Physics und parentierte Entitäten

**Fundstelle:** [Runtime.PhysicsModule.cpp](../../src/runtime/Modules/PhysicsIntegration/Runtime.PhysicsModule.cpp),
Zeile 681 (`TryBuildDescriptor`) und 930–936 (Writeback).

**Fall und Folge:** Body unter um 100 Einheiten verschobenem Parent wird mit
seiner lokalen ECS-Pose in Physics angelegt. Die Physics-Weltpose wird direkt
ins lokale TRS zurückgeschrieben. Kollisionsort und sichtbare Weltpose können
auseinanderfallen; Parent-Bewegungen fehlen im Authoring-Fingerprint.

**Kleinster Fix und Test:** Bis zur korrekten Unterstützung parentierte Bodies
ausdrücklich ablehnen/diagnostizieren. Vollständige Unterstützung braucht Weltpose
beim Eingang und Parent-relative Pose beim Writeback, einschließlich Behandlung
nicht darstellbarer Ergebnisse. Parent/Kind und Parent-Bewegung in
[Test.PhysicsModule.cpp](../../tests/integration/runtime/Test.PhysicsModule.cpp)
prüfen; bisherige Fixtures erzeugen Bodies ohne Hierarchy/WorldMatrix.
Zuständigkeit bleibt Runtime, nicht ECS oder Solver.

**Überschneidung:** Derselbe Vertrag ist im
[Runtime-Bericht](2026-10-16-src-runtime-audit.md) als `SRC-RUNTIME-005`
und im [Physics-Bericht](../reports/2026-10-16-src-physics-audit.md) als
`PHYS-AUDIT-02` beschrieben. Gemeinsam bearbeiten und alle Befundkennungen
referenzieren. Physics bewertet die Priorität als P1, dieser Bericht als P2;
die Gesamtauswertung muss sie am benötigten Hierarchieszenario festlegen.

### ECS-20261016-012 Nicht unterstützte Physics Absichten

**Fundstelle:** `Runtime.PhysicsModule.cpp`, Zeile 361 ff. (`ConvertShape`) und
688–700 (`TryBuildDescriptor`); Descriptoren in
[RigidBody.cppm](../../src/ecs/Components/ECS.Component.RigidBody.cppm) und
[Collider.cppm](../../src/ecs/Components/ECS.Component.Collider.cppm).

**Fall und Folge:** Bei `MassPolicy::Density` wird trotzdem `Mass.Mass`
übertragen. Density-Änderungen und Collider-Filtermasken bleiben wirkungslos;
weitere Felder wie Material/Offsets/CCD werden nicht vollständig transportiert.
Eine entsprechende Diagnose fehlt.

**Bewusste Grenze:** [Physics-Architektur](../architecture/physics.md), Zeile
134 ff., begrenzt unterstützte Filterung auf Kontaktteilnahme, Enabled-Flags
und Trigger. Die breitere ECS-Authoring-Oberfläche ist bewusst vorhanden;
das Finding fordert keine vollständige Physics-Implementierung.

**Kleinster Fix und Test:** Unterstützten Runtime-Teilvertrag prüfen und
abweichende, noch nicht unterstützte Absichten sichtbar melden/ablehnen.
Density-Policy und gesetzte Filtermasken in `Test.PhysicsModule.cpp` testen;
Diagnoseverhalten festlegen, bevor Solver-Funktionen ergänzt werden.

**Überschneidung:** [Physics-Bericht](../reports/2026-10-16-src-physics-audit.md),
`PHYS-AUDIT-03`, beschreibt denselben Adaptervertrag. Auch dort ist die sichtbare
Behandlung nicht unterstützter Werte von einem vollständigen Solver-Ausbau getrennt.

## Dokumentation und kleine Bereinigung

### ECS-20261016-013 Veraltete ECS Dokumentation

**Fundstellen und Folgen:** [src/ecs/README.md](../../src/ecs/README.md), Zeilen
43/48, nennt singuläre Module für AssetInstance/Selection, tatsächlich exportiert
als `Components.*`. Zeilen 284–285 behaupten `ObserverPtr`, nichtbesitzende
PropertySets und `Tetrahedra`; der Code besitzt vier PropertySets und kein
Tetrahedra-Component. Zeilen 256–258 behandeln den existierenden Runtime-Lookup
noch als Zukunft. [Systems/README.md](../../src/ecs/Systems/README.md) beschreibt
den früheren Weltkugel-Skalierungsalgorithmus. Der Kommentar in
[RenderSync.cpp](../../src/ecs/Systems/ECS.System.RenderSync.cpp), Zeile 40,
nennt nur TransformHierarchy als Producer von `WorldUpdatedTag`, obwohl auch
PhysicsModule das Tag setzt.

**Kleinster Fix und Prüfung:** READMEs als kurze aktuelle Einstiege halten,
Verträge einmal in [docs/architecture/ecs.md](../architecture/ecs.md) pflegen und
verlinken. Falsche Imports/Ownership korrigieren, doppelte History-Prosa kürzen,
Kommentar aktualisieren. Keine begleitende Umbenennung durch alle Imports.
Doc-Link-/Docs-Sync-Prüfungen für reine Textänderungen verwenden.

### ECS-20261016-014 Ungenutztes PickID

**Fundstelle:** [ECS.Component.Selection.cppm](../../src/ecs/Components/ECS.Component.Selection.cppm),
Zeilen 20–25, `Selection::PickID`.

**Beleg und Folge:** Whole-tree-Suche fand keine C++-Leser, -Schreiber oder
Tests, nur Definition, Dokumentation und Kommentare. Tatsächliche Render-IDs
stammen aus `StableEntityLookup::ToRenderId` (`Runtime.RenderExtraction.cpp`,
Zeile 147). Ein gesetztes `PickID.Value` beeinflusst den vorhandenen Picking-Pfad nicht.

**Kleinster Fix und Prüfung:** Typ und überflüssiges `<cstdint>` entfernen,
Dokumentation bereinigen. Vor Löschung auf aktuellem Stand erneut alle Consumer
suchen und betroffene Module/Testtargets bauen. Potenzial: ungefähr sieben
Codezeilen, keine Dependency. Keinen zweiten ID-Pfad nur zur Belebung des Typs einführen.

## Ausgeführte Verifikation

Die Resultate stammen aus der Audit-Sitzung am genannten Commit. Für das
anschließende Speichern wurden die C++-Tests nicht erneut gestartet.

```bash
cmake --build --preset ci --target IntrinsicECSTests --parallel 4
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(ECS|EcsLayeringBoundaries)'
python3 tools/repo/check_layering.py --root src --strict
```

Build erfolgreich; 113 Tests bestanden (112 Unit-Tests und eine SourceRules-CTest).
Keine Layering-Verletzung bei 960 Dateien, 10.266 Import-/Include- und 89
CMake-Linkreferenzen. Konfiguration: `ci`, Clang 23 mit passendem
`clang-scan-deps`, Debug, headless, ohne Sanitizer.
Die Layering-Prüfung wurde beim Speichern des Berichts zur Ablage der Ausgabe
erneut ausgeführt; dieselben Zahlen stehen lokal in `report-layering.log`.

Die externen Assert-Probes importierten die tatsächlich gebauten ECS-/Geometry-
Module und linkten deren Bibliotheken. Vor Ausführung war festgelegt: zwei
Kontrollen bestehen; sieben Fehlerfälle verletzen ihre Soll-Invariante. Das
sind Fehlerreproduktionen, keine grünen Produktionsregressionen. Der Wrapper
prüft derzeit die erwartete Fehlerreproduktion; für dauerhafte Fix-Tests muss
die jeweilige Soll-Assertion ohne erwarteten Abbruch übernommen werden.

| Probe | Befund | Beobachtetes Ergebnis |
| --- | --- | --- |
| `attached_control` | Kontrolle zu 001 | Exit 0, Kind-Weltposition 12 |
| `cloud_control` | Kontrolle zu 004–006 | Exit 0, Property-/Positionszeilen jeweils 3 |
| `detach` | 001 | Root lokal 2, global 12, dirty 0; Assert-Abbruch |
| `reparent_shear` | 002 | Maximale Matrixdifferenz 0,424264; Assert-Abbruch |
| `nonfinite_transform` | 003 | accepted=1, position.x=inf; Assert-Abbruch |
| `cloud_conflict` | 004 | Ungültiger PropertyBuffer; Assert-Abbruch |
| `cloud_deleted` | 005 | NumDeleted=1, p:deleted vorhanden, v:deleted fehlt; Assert-Abbruch |
| `cloud_view` | 006 | Property-Zeilen 3, Positionszeilen 1; Assert-Abbruch |
| `bounds_reflection` | 007 | Ecke (1,2,-1) außerhalb Extents (2,1,1); Assert-Abbruch |

### Lokale Reproduktion und Evidenz

Eingaben, Symbole und Sollprüfungen stehen in den Befunden, damit der Bericht
ohne Sitzungsdateien nutzbar bleibt. Vollständiger Probe-Code, Runner,
JSON-Ergebnisse und Build-/CTest-Logs liegen gemäß Repository-Regel lokal in
`tasks/evidence/SRC-ECS-AUDIT-2026-10-16/` (ignoriert, nicht versioniert).
Es sind bytegleich geprüfte Kopien aus
`/home/alex/Documents/Codex/2026-10-16/bitte-schaue-dir-src-assets-genau/work/`;
deshalb enthalten die ursprünglichen Assert-Ausgaben diesen Ausführungspfad.
`evidence-files.sha256` hält die Digests der sieben übernommenen Dateien fest.

```bash
# Im Repository, nach dem oben genannten fokussierten Build.
# Der Runner schreibt neben sich: Original-Evidenz nie direkt überschreiben.
ecs_repro_dir=$(mktemp -d tasks/evidence/SRC-ECS-AUDIT-2026-10-16/recheck.XXXXXX)
cp tasks/evidence/SRC-ECS-AUDIT-2026-10-16/ecs-audit-probes.cpp "$ecs_repro_dir/"
cp tasks/evidence/SRC-ECS-AUDIT-2026-10-16/run-ecs-audit-probes.py "$ecs_repro_dir/"
ulimit -c 0
python3 "$ecs_repro_dir/run-ecs-audit-probes.py"
```

Der Runner verwendet `build/ci/compile_commands.json` und dessen Modulzuordnungen
sowie den absoluten Repository-Pfad dieser Maschine. Bei anderem Checkout den
Pfad anpassen. Alte Build-Artefakte sind kein Nachweis für veränderten Code.
Der erste Kompilierversuch verwendete versehentlich `-pthread`, inkompatibel
zur PCM-Konfiguration. Nach reinem Linken mit `-lpthread` liefen die obigen
Probes; keine Vorhersage wurde dafür geändert.

### CI Kontext

Am Auditbeginn war [ci-docs](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38869189384)
grün. [ci-linux-clang](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38869189381)
war rot: `Capture CPU test selection` fand für
`RenderWorldPoolEngineWiring.SourceRules` keinen registrierten Produzenten.
Fehlende Test-Timingdaten waren ein Folgefehler, kein gesonderter ECS-Testfehler.
Auch der letzte [nightly-deep-Lauf](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38854737500)
war rot. Diese CI-Probleme sind getrennt von den ECS-Befunden; der fokussierte
grüne Lauf ist keine allgemeine CI-Freigabe.

Nicht ausgeführt: vollständige Runtime-Suite, laufende Sandbox, GPU-/Vulkan-
Tests, Sanitizer und Performance-Benchmarks. Physics-/Save/Load-Folgen bleiben
`S`-Befunde. Die Cloud-Löschmaske ist lokal reproduziert, ihr Consumer-Effekt
aus Code hergeleitet. Es gibt keine wissenschaftliche Ergebnis- oder
Performancebehauptung.

## Empfohlene Bearbeitung für den nächsten Agenten

1. Aktuellen Git-/CI-Stand prüfen und mit den anderen
   [src-Berichten](src-audits.md) abgleichen. Grenzbefunde nach Ursache
   zusammenführen; ursprüngliche IDs als Referenzen erhalten.
2. **Cloud 004–006:** vorhandene Normalisierung wiederverwenden; Typkonflikte,
   Löschmaske und Teilansichten durch Regressionen absichern.
3. **Transformationen 001–003:** Detach und TRS-Validierung konsistent machen;
   Fehler vor Teilmutation behandeln und Runtime-Zusatzprüfungen berücksichtigen.
4. **Bounds 007–008:** einschließende Volumen gewährleisten, vorhandene
   OBB→AABB-Konversion nutzen, beide Extraktionspfade prüfen.
5. **Szenendokumente 009–010:** Fehler beim Verknüpfen müssen die alte Szene
   erhalten; Geschwisterfolge durch beide Save-Pfade tragen.
6. **Physics 011–012:** unterstützten Teilvertrag sichtbar machen;
   Parent-Unterstützung beziehungsweise ausdrückliche Ablehnung festlegen.
7. **Dokumentation und Löschung 013–014:** aktuelle Verträge knapp dokumentieren,
   nutzlose Oberfläche entfernen; keine breite Umbenennung beimischen.

Jeder Fixblock dient Frage 4; die Bereinigung zusätzlich Fragen 1 und 3.
Frage 2 wird durch Beibehalten der tragfähigen Struktur beantwortet. Dies ist
eine Reihenfolgeempfehlung, keine Autorisierung, alle Blöcke ungefragt umzusetzen.
Beim jeweiligen Fix Aufrufer und Tests zusammen ändern, Regression vorher
scheitern lassen und anschließend fokussiert verifizieren. Der ECS-Backlog
enthielt zum Auditzeitpunkt keine offenen Tasks; die Report-IDs eröffnen keine.
