# Runtime Audit und Übergabe

## Auftrag und ursprüngliche Fragen

1. „sollten wir runtime aufräumen?“
2. „anders organisieren?“
3. „hat die implementierung einen durchgenden stil?“
4. „gibt es offene punkte, bugs, implementierungs gaps?“

Anschließender Auftrag: „Erstelle einen Bericht aus deinen Ergebnissen in IntrinsicEngine so dass Codex 6 Astra Ultra anschließend alles finden kann. Ich lasse für alle weiteren unterordner von IntrinsicEngine/src von anderen Agenten ebenfalls so einen Bericht erstellen“.

Dieser Bericht ist die eigenständig lesbare Übergabe des Runtime-Audits an
Codex 6 Astra Ultra und die spätere bereichsübergreifende Auswertung. Er dient dem
Repository-Ziel einer modularen, testbaren Engine mit vollständigen Nutzerabläufen.
Die kürzeste Verbesserung ist die Reparatur konkreter Daten- und
Lebenszyklusfehler, danach die Vereinheitlichung ihrer bereits vorhandenen Regeln.

Einstieg in die gemeinsame Serie: [Berichte zu src](src-audits.md).

## Stand und Verwendung

| Merkmal | Wert |
| --- | --- |
| Auditdatum | 2026-10-16, Europe/Berlin |
| Repository | IntrinsicEngine |
| Geprüfter Commit | `66da33fd6bfabb20408607b8361354fc9adb420d` |
| Primärer Bereich | `src/runtime` mit gezieltem Nachverfolgen in `src/ecs`, `src/geometry`, `src/graphics`, `src/app`, `tests` und Architekturdocs |
| Umfang | 320 Dateien im Runtime-Verzeichnis, davon 130 unter `Editor`; risikoorientierte Tiefenprüfung, keine Volllektüre aller Dateien |
| Angenommene Nutzung | Ein Sandbox-/Editor-Prozess, Main-Thread für Szenenänderungen, CPU-Worker für Jobs, optionale Vulkan- und Rendering-Pipeline-Pfade |
| Ergebnisstatus | Audit abgeschlossen; keine der unten beschriebenen Runtime-Reparaturen umgesetzt |
| Belege | Vier Defekte praktisch reproduziert; weitere Befunde statisch nachvollzogen, teilweise unabhängig gegengeprüft |

Die Kennungen `SRC-RUNTIME-001` bis `SRC-RUNTIME-014` sind **Befundkennungen**,
keine bestehenden `BUG-`-/`RUNTIME-`-Tasks und keine Implementierungsfreigaben.
Sie bleiben bei späteren Verweisen stabil. Vor einer Reparatur den aktuellen
Code und inzwischen angelegte Tasks prüfen. Zeilennummern beziehen sich auf den
oben genannten Commit; die genannten Symbole sind die Suchanker bei Verschiebungen.

Für eine Gesamtauswertung nach Pfad und verletztem Vertrag deduplizieren:
Ein Runtime-Adapterfehler ist nicht automatisch ein Fehler des darunterliegenden
Geometry-, ECS- oder Graphics-Subsystems. Insbesondere gelten 002 für die
Runtime-History, 005 für ECS-zu-Physics und 006 für Runtime-zu-Renderer.

## Ergebnis und Prioritäten

Die vorhandene Verantwortungsteilung beibehalten. Zuerst Absturz, Undo und
Speichersemantik reparieren. Anschließend gemeinsame Validierung und
Persistenzregeln konsolidieren. Neue Registry-, Service- oder Framework-Schichten
sind durch die Befunde nicht begründet.

| ID | Priorität | Befund | Belegstatus | Primärer Owner |
| --- | --- | --- | --- | --- |
| SRC-RUNTIME-001 | P1 | Sampling mit falsch typisiertem Ausgabenamen stürzt ab | Reproduziert, zwei Prüffälle | Editor/Operations |
| SRC-RUNTIME-002 | P1 | Zweites Undo nach zwei Property-Änderungen scheitert | Sampling reproduziert; weitere Pfade statisch | Editor/Operations und History |
| SRC-RUNTIME-003 | P1 | Queued Save markiert spätere Edits als gespeichert | Reproduziert | SceneDocumentModule |
| SRC-RUNTIME-004 | P2 | Queued Save verliert VertexChannelBindingSet | Quellpfad bestätigt | SceneDocumentModule und Serializer |
| SRC-RUNTIME-005 | P2 | Parented Physics verwendet lokale statt Weltkoordinaten | Unabhängig gegengeprüft, nicht ausgeführt | Modules/PhysicsIntegration |
| SRC-RUNTIME-006 | P2 | Pipelined Rendering übernimmt Snapshot der alten Welt | Unabhängig gegengeprüft, nicht ausgeführt | Rendering und Engine |
| SRC-RUNTIME-007 | P2 | Ungültige Agent-Argumente können Undo ausführen | Unabhängig gegengeprüft, nicht ausgeführt | Agent |
| SRC-RUNTIME-008 | P2 | Akzeptierter Outlier-Radius wird zu ungültigem Nullradius | Reproduziert | Config |
| SRC-RUNTIME-009 | P2 | Config-Feldtabellen und Schemaabdeckung unvollständig | Bestehender Gap, RUNTIME-276 | Config |
| SRC-RUNTIME-010 | P2 | Job-Priorität wird angenommen, aber nicht umgesetzt | Statisch; kein Deadlock nachgewiesen | Kernel/JobService |
| SRC-RUNTIME-011 | P2 | RenderWorldPool verspricht ungesicherte Nebenläufigkeit | Vertragslücke; aktueller Engine-Aufruf seriell | Rendering |
| SRC-RUNTIME-012 | P3 | Abbruch eines wartenden Jobs wird doppelt gezählt | Statisch, nicht ausgeführt | Kernel/JobService |
| SRC-RUNTIME-013 | P3 | Runtime-README enthält widersprüchliche Persistenzangaben | Dokument und Implementierung verglichen | Runtime-Dokumentation |
| SRC-RUNTIME-014 | P3 | Dateinamen spiegeln teilweise frühere Modulowner | Mechanischer Navigationsbefund | Editor/Operations |

## Fehler mit unmittelbarer Nutzerwirkung

### SRC RUNTIME 001 Sampling Ausgaben müssen bestehende Property Typen prüfen

**Auslöser und Folge:** Ein Point-Set hat `v:position` und eine `vec3`-Property
`v:normal`. Sampling mit `RankName = "v:normal"` wird in der Vorschau freigegeben.
Die Ausführung fordert `GetOrAdd<float>(rankName)` an und ruft ungeprüft `Vector()`
auf. Das Ergebnis ist ungültig, weil der Name bereits mit anderem Typ belegt ist.
Im Debug-Build endet der Aufruf mit SIGABRT. Ein Konflikt der Bool-Ausgabe kann
erst nach Schreiben der Rank-Ausgabe auftreten und gefährdet zusätzlich die Atomizität.

**Fundstellen:** [Capture und Publication](../../src/runtime/Editor/Operations/Runtime.PointSamplingOperations.cpp)
:121–140, :193–203; [PropertyRegistry::GetOrAdd und PropertyBuffer::Vector](../../src/geometry/Geometry.Properties.cppm)
:742–754 und :411–415. Die Property-API verhält sich hier vertragsgemäß; Runtime
prüft das ungültige Ergebnis nicht.

**Kleinster Fix:** Das vorhandene `ValidatePointOutputs` aus
[PointProperties](../../src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.PointProperties.cpp)
:175–189 für beide typisierten Ausgaben verwenden, bevor eine Mutation beginnt.
Vorschau und Apply müssen dieselbe Prüfung nutzen.

**Regression:** Falsch typisierte Rank- und Selected-Namen separat prüfen:
Vorschau deaktiviert, Apply liefert Fehler ohne Absturz, Properties und History
unverändert. Owner: [Test.PointSamplingOperations.cpp](../../tests/contract/runtime/Test.PointSamplingOperations.cpp).
Vorhandene Tests :124–151 prüfen identische/reservierte Namen, nicht diesen Typkonflikt.

**Ausgeführt:** `RuntimeAudit.SamplingRejectsWrongTypeOutputBeforeExecution`
scheiterte mit `Enabled == true`; `RuntimeAudit.WrongTypeSamplingOutputReturnsFailureWithoutCrash`
endete im Kindprozess mit Signal 6 und `m_Storage != nullptr` an `Geometry.Properties.cppm:413`.

### SRC RUNTIME 002 History verwechselt zurückgestellte Werte mit unveränderter Revision

**Auslöser und Folge:** Sampling A und danach B schreiben dieselben Rank-/Selected-
Properties. Undo B stellt A-Werte wieder her, vergibt dafür aber neue Revisionen.
Undo A erwartet noch seine private alte Revision und liefert `StaleEntity`.
Zwei gewöhnliche aufeinanderfolgende Edits lassen sich damit nicht vollständig zurücknehmen.

**Fundstellen:** [Sampling mutate](../../src/runtime/Editor/Operations/Runtime.PointSamplingOperations.cpp)
:193–205; [PointPropertyWatch](../../src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp)
:40–46; [PropertyRevisionState::MarkModified und Observe](../../src/geometry/Geometry.Properties.cpp)
:26–45; [EditorCommandHistory::Undo](../../src/runtime/Editor/Runtime.EditorCommandHistory.cpp)
:127–143. Dasselbe Revisionsmuster steht in
[PublishPointScalarField](../../src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.PointProperties.cpp)
:590–607 und [Positionspublication](../../src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.GpuPositions.cpp)
:204–220. Diese beiden weiteren Ausführungsfälle wurden nicht reproduziert.

**Fixrichtung:** Async-Stale-Prüfung und History-Replay-Prüfung unterscheiden.
History muss die erwartete Identität und den wiederhergestellten Zustand erkennen
können, statt eine alte Schreibrevision zu verlangen. Vorhandene bitgenaue
Vergleichshelfer prüfen. Async-Versionierung und Ablehnung fremder Edits erhalten;
die Revisionsprüfung nicht pauschal entfernen.

**Regression:** A → B → Undo → Undo → Redo → Redo für dieselben Ausgaben;
zusätzlich ein Edit außerhalb dieser History, der weiterhin abgelehnt werden muss.
Sampling zuerst, dann Scalar- und Positions-Owner. Vorhandene Einzel-Undo-Tests
decken die Kombination nicht ab.

**Ausgeführt:** `RuntimeAudit.TwoSamplingEditsCanBothBeUndone` mit 20 Punkten und
Sampling-Counts 8, dann 5. Beide Applys und das erste Undo erfolgreich; zweites
`Undo().Succeeded()` tatsächlich `false`, erwartet `true`.

### SRC RUNTIME 003 Save muss die Snapshot Revision speichern

**Auslöser und Folge:** Save erfasst Revision 1. Vor dem Main-Thread-Abschluss
erfolgt ein Edit zu Revision 2. Die Datei enthält den früheren Snapshot, aber
die History markiert Revision 2 als gespeichert. Ein unsaved-change-Hinweis kann fehlen.

**Fundstellen:** [QueueSceneSaveToPath](../../src/runtime/Scene/Runtime.SceneDocumentModule.cpp)
:874–879 erfasst den Snapshot, :953–957 ruft bei Abschluss `History.MarkSaved(path)`
auf. [MarkSaved](../../src/runtime/Editor/Runtime.EditorCommandHistory.cpp):187–191
übernimmt die aktuelle Revision. Binding-Prüfungen :401–410 im DocumentModule
prüfen Welt/Epoch/Registry, nicht die Inhaltsrevision. Der Editor nutzt den
queued Pfad über [ContextAdapters](../../src/runtime/Editor/internal/Runtime.EditorFeatureContextAdapters.cpp):1971–1973.

**Kleinster Fix:** History-Revision beim Snapshot mitführen und genau diese als
gespeichert markieren. Nachfolgende Edits bleiben dirty. Reihenfolge mehrerer
Save-Aufträge auf denselben Pfad bei der Reparatur mitprüfen, ohne hier bereits
einen weiteren nachgewiesenen Fehler zu behaupten.

**Regression:** Bestehendes `WaitUntilAwaitingGate` in
[Test.SceneDocumentModule.cpp](../../tests/contract/runtime/Test.SceneDocumentModule.cpp)
verwenden: Snapshot erfassen, Worker bis Apply-Gate warten lassen, Edit ausführen,
Completion drainen, `Dirty == true` und `SavedRevision == snapshotRevision` prüfen.

**Ausgeführt:** `RuntimeAuditSave.LaterEditRemainsDirtyWhenQueuedSaveCompletes`.
Tatsächlich `Dirty == false`, `SavedRevision == 2`; erwartet dirty und Revision 1.
Der zusätzliche Prüffall verwendet `MarkDirty` als deterministischen History-Edit.

### SRC RUNTIME 004 Queued Save muss alle serialisierbaren Komponenten übernehmen

**Auslöser und Folge:** Ein Nutzer bindet Farbe, Normale oder Radius an eine
Property, speichert über den Editor und lädt erneut. Die Properties überleben,
die expliziten Attributbindungen fehlen im Save-Snapshot.

**Fundstellen:** [SnapshotSerializableScene](../../src/runtime/Scene/Runtime.SceneDocumentModule.cpp)
:180–219 kopiert Rendering-/Presentation-Komponenten, aber kein `VertexChannelBindingSet`.
[SceneSerialization](../../src/runtime/Scene/Runtime.SceneSerialization.cpp):2975–2983
serialisiert diese Komponente ausdrücklich. Der direkte Serializer-Roundtrip in
[Test.RuntimeSceneSerialization.cpp](../../tests/contract/runtime/Test.RuntimeSceneSerialization.cpp):1387
umgeht den fehlerhaften Snapshot.

**Kleinster Fix:** `VertexChannelBindingSet` mitkopieren. Danach den Vertrag zwischen
Snapshot und Serializer an einer gemeinsamen, überprüfbaren Stelle absichern;
ein generisches Serialisierungsframework ist dafür nicht erforderlich.

**Regression:** Attributbindung setzen → queued Save → Load → Bindung und Property
vergleichen, über `Test.SceneDocumentModule.cpp`. Status: Quellpfad bestätigt,
dieser neue End-to-End-Test wurde im Audit nicht ausgeführt.

### SRC RUNTIME 005 Physics muss Parent Transformationen berücksichtigen

**Auslöser und Folge:** Ein Collider liegt lokal bei x=0 unter einem Parent bei
x=100. Sichtbar befindet er sich bei x=100, die Physics-Pose wird aber bei x=0
angelegt. Ein rotierter Parent verdreht außerdem die sichtbare Richtung der
Simulation, weil Solver-Posen unmittelbar als lokale TRS zurückgeschrieben werden.

**Fundstellen:** [PhysicsModule::ConvertTransform und TryBuildDescriptor](../../src/runtime/Modules/PhysicsIntegration/Runtime.PhysicsModule.cpp)
:342–349, :678–681; Writeback :930–934. Das ECS kennzeichnet
[Transform::Component](../../src/ecs/Components/ECS.Component.Transform.Local.cppm):19
als lokal. [TransformHierarchy](../../src/ecs/Systems/ECS.System.TransformHierarchy.cpp):40
berechnet Parent × Local. Der [Physics-Vertrag](../architecture/physics.md):212,219
fordert Weltkoordinaten; die Aussage zu Compound-Collidern schließt Parented Bodies nicht aus.

**Fixrichtung:** Weltpose zur Physics geben und beim Writeback mit vorhandenem
`TryComputeLocalTransform` in lokale Koordinaten zurückrechnen; nicht darstellbare
Transformationen validieren. Falls der Nutzer Hierarchieunterstützung bewusst
verschiebt, solche Bodies stattdessen mit Diagnose ablehnen. Stilles falsches
Simulieren ist kein akzeptabler Fallback.

**Regression:** Translatierter und rotierter Parent, jeweils Collider-Position
und dynamischer Writeback. [Test.PhysicsModule.cpp](../../tests/integration/runtime/Test.PhysicsModule.cpp)
:370,482 prüft die entsprechenden unparented Fälle. Status: von zwei Reviewern
unabhängig bestätigt, kein Physics-Reproduktionslauf im Audit.

**Überschneidung:** Der parallel entstandene [Physics-Bericht](../reports/2026-10-16-src-physics-audit.md)
führt denselben Adapterfehler als `PHYS-AUDIT-02`. Bei der Gesamtauswertung mit
`SRC-RUNTIME-005` zusammenführen; die unterschiedliche Priorität gemeinsam bewerten.

### SRC RUNTIME 006 Weltwechsel muss die Pipeline Historie invalidieren

**Auslöser und Folge:** `SynchronousExtraction=false`; bisherige Front liegt in
Slot 1 oder 2; aktive Welt wechselt. Der nächste Frame kann alte Renderables,
Selection, Lights und Debug-Pakete mit neuer Frame-/Kameraeingabe kombinieren.
Default `true` verwendet einen einzelnen Slot und ist von diesem Ablauf nicht betroffen.

**Fundstellen und Reihenfolge:**

1. [Engine](../../src/runtime/Kernel/Runtime.Engine.cpp):1154–1166 gibt die Front frei
   und führt World-Maintenance aus; :492–493 leert den Extraction-Cache.
2. [ClearSceneState](../../src/runtime/Rendering/Runtime.RenderExtraction.cpp):2629
   ruft `SubmitRuntimeSnapshots({})` ohne Slot auf. Der Default ist 0 in
   [Renderer-Schnittstelle](../../src/graphics/renderer/Graphics.Renderer.cppm):231–232;
   [Implementierung](../../src/graphics/renderer/Graphics.Renderer.cpp):2095 leert nur diesen Slot.
3. Engine :1056–1089 publiziert den neuen Back-Slot und konsumiert anschließend
   `AcquirePreviousFront`. [Pool](../../src/runtime/Rendering/Runtime.RenderWorldPool.cpp)
   :31–40 schließt die alte Front beim Überschreiben aus, :130–134 behält sie als PreviousFront.
4. Renderer :2542–2606 übernimmt die adressierten Snapshot-Daten ohne World-ID-Prüfung.

**Kleinster Fix:** Bei Welt-/Dokumentwechsel alle konsumierbaren Snapshot-Slots
invalidieren und Poolhistorie neu initialisieren beziehungsweise den ersten
gültigen Frame der neuen Welt synchron konsumieren.

**Regression:** Front auf Slot 1 bringen → Wechsel zu leerer Welt → Pipeline-Frame;
keine alten Renderables, Auswahl, Lichter oder Overlays dürfen erscheinen.
[Test.RenderWorldPoolPipelined.cpp](../../tests/integration/runtime/Test.RenderWorldPoolPipelined.cpp):67
prüft nur die unveränderte Szene; [Test.RuntimeWorldRegistry.cpp](../../tests/contract/runtime/Test.RuntimeWorldRegistry.cpp):518
prüft World-Switch ohne diese Kombination.

**Grenze des Befunds:** Zwei Reviewer bestätigen die alte Snapshot-Übernahme.
Kein GPU-Use-after-free oder Absturz nachgewiesen; `GpuWorld::FreeInstance`
und nachgelagerte Handleprüfungen begrenzen mögliche Folgen. Nicht ausgeführt.

### SRC RUNTIME 007 Ungültige Agent Argumente dürfen keine Default Mutation auslösen

**Auslöser und Folge:** `tools/call` mit `name:"undo"` und `arguments:[]` oder
`arguments:{"steps":-1}` beziehungsweise `{"steps":"2"}` kann einen Undo-Schritt
ausführen. Das angekündigte JSON-Schema wird nur veröffentlicht, nicht vorher erzwungen.

**Fundstellen:** [AgentProtocol::Handle](../../src/runtime/Agent/Runtime.AgentServer.cpp)
:181–183 ersetzt nicht objektförmige Argumente durch `{}`.
[UndoRedo](../../src/runtime/Agent/Runtime.AgentOperations.Editor.cpp):675–677
ersetzt fehlgeschlagenes `UInt`-Parsing durch 1; :682–685 mutiert die History.
[InvokeAgentOperation](../../src/runtime/Agent/Runtime.AgentOperations.cpp):95–106
enthält keine weitere Argumentvalidierung.

**Kleinster Fix:** Vorhandene nicht objektförmige `arguments` an der Protokollgrenze
ablehnen. In UndoRedo nur bei fehlendem `steps` den Default verwenden;
vorhandene ungültige Werte zurückweisen.

**Regression:** Negative, String-, Array- und außerhalb uint32 liegende Werte;
History und Szene unverändert. Fehlendes `steps` bleibt ein gültiger Einzelschritt.
Owner: [Test.AgentOperations.cpp](../../tests/contract/runtime/Test.AgentOperations.cpp)
und [Test.SandboxAgentServer.cpp](../../tests/integration/runtime/Test.SandboxAgentServer.cpp).
Status: unabhängig gegengeprüfter Quellpfad, kein neuer Ausführungstest.

### SRC RUNTIME 008 Outlier Radius muss nach Float Konvertierung positiv bleiben

**Auslöser und Folge:** `{"method":"radius","radius":1e-100}` besteht die
Finite-/Positivprüfung auf `double`. `OutlierAnalysisConfig::Radius` ist `float`;
die Konvertierung liefert 0. Der Validator meldet dennoch nutzbare Konfiguration,
deren kanonischer Payload von derselben Validierung abgelehnt wird.

**Fundstellen:** [Outlier-Codec](../../src/runtime/Config/internal/Runtime.PointNeighborhoodConfigCodecs.cpp)
:36, :77–88 und [Radius-Feld](../../src/runtime/Modules/OutlierAnalysis/Runtime.OutlierAnalysisConfig.cppm):31.
KDE (:145–146), PointSpacing (:211–212) und
[Bilateral](../../src/runtime/Config/internal/Runtime.PointFeaturesConfigCodecs.cpp):67–69
enthalten bereits einschlägige Darstellbarkeitsprüfungen.

**Kleinster Fix:** Positive Darstellbarkeit als Float prüfen und den bestehenden
Prüfmechanismus wiederverwenden. `DensityWeight::SupportRadius` ist `double` und
ist ausdrücklich kein zweites Beispiel dieses Fehlers.

**Regression:** Unterlauf sowie Invariante „jeder akzeptierte kanonische Payload
lässt sich erneut validieren“ in [Test.OutlierAnalysis.cpp](../../tests/contract/runtime/Test.OutlierAnalysis.cpp).
Vorhandene Negativfälle :252–256 enthalten Null und Überlauf, nicht positiven Unterlauf.

**Ausgeführt:** `RuntimeAuditConfig.PositiveRadiusMustRemainPositiveInCanonicalPayload`.
Erste Validierung nutzbar; Ausgabe enthält `"radius":0.0`; erneute Validierung unbrauchbar.

## Konsistenz und nachrangige Vertragslücken

### SRC RUNTIME 009 Bestehende Config Migration abschließen

Outlier, KDE und Spacing verwenden handgeschriebene Felder, Enum-Tokens und
Grenzprüfungen. [MakeSectionRegistration](../../src/runtime/Config/internal/Runtime.FeatureConfigCodecs.Detail.cpp)
:85–101 setzt kein `SchemaJson`. Neuere Familien verwenden `ConfigFieldSpec`
mit Schemaerzeugung. [Test.EngineConfigSectionSchemas.cpp](../../tests/integration/runtime/Test.EngineConfigSectionSchemas.cpp):56
überspringt fehlende Schemas; der Agent erhält für diese Familien keine vollständigen Metadaten.

Das ist bestehende Arbeit in [RUNTIME-276](../../tasks/done/RUNTIME-276-declarative-config-field-specs.md),
kein Anlass für einen zweiten Schema-Unterbau. Familie für Familie auf den
vorhandenen Owner migrieren, Cross-Field-Prüfungen behalten und Validator/Schema-
Übereinstimmung testen. Alte Familien verwenden teilweise String-Enums; der Task
enthält eine Integer-Enum-Zielentscheidung. Diesen Widerspruch zum Ist-Format
vor einer Formatänderung ausdrücklich klären und alle aktuellen Caller aktualisieren.

### SRC RUNTIME 010 Job Priorität besitzt einen Caller aber keine Wirkung

[JobService](../../src/runtime/Kernel/Runtime.JobService.cpp):376–378 übernimmt
`Priority`, `Kind` und `EstimatedCost`, liest sie im Dispatch aber nicht;
:420–421 dispatcht immer `Background`. Ein realer
[CPD-Caller](../../src/runtime/Editor/Operations/Runtime.RegistrationOperations.CoherentPointDrift.cpp):790–791
fordert High-Priorität, damit sein Vulkan-E-Step-Pump vor einem wartenden Worker läuft.

Kein Deadlock nachgewiesen: Der Pump wird gegenwärtig zuerst submitted. Trotzdem
sind API und Erwartung verschieden. Worker-only-Ausführung erhalten; die tatsächlich
benötigte Prioritätssemantik implementieren oder den Caller-Vertrag explizit ändern.
Kind/Cost nur entfernen, wenn die vollständige Callersuche ihren fehlenden Nutzen bestätigt.
Regression: deterministisch belegte Reihenfolge konkurrierender Jobs sowie
weiterhin keine Ausführung auf dem Main-Thread. Statischer Befund.

### SRC RUNTIME 011 RenderWorldPool Nebenläufigkeitsvertrag begrenzen oder implementieren

[AcquireFront](../../src/runtime/Rendering/Runtime.RenderWorldPool.cpp):145–156
liest die Front vor dem Refcount-Inkrement. Ein paralleler Producer könnte
dazwischen eine andere Front publizieren und die alte zurückgewinnen.
`PendingReclaim` wird außerdem ohne atomare Synchronisation geschrieben (:203).
Die [Schnittstelle](../../src/runtime/Rendering/Runtime.RenderWorldPool.cppm):40–43,66–68
verspricht Producer-/Consumer-Nutzung.

Der aktuelle Engine-Caller nutzt den Pool seriell. Dieser Befund ist daher eine
unimplementierte API-Zusage, kein reproduzierter Crash des Default-Pfads.
Ponytail-Empfehlung: Main-Thread-only-Vertrag klarstellen, solange kein echter
paralleler Caller gebraucht wird. Falls Nebenläufigkeit erhalten werden soll,
Acquire/Publish/Reclaim gemeinsam absichern und gezielte Interleavings testen.
Sequenzielle Pooltests beweisen keine Thread-Sicherheit.

### SRC RUNTIME 012 Wartende Jobs werden bei explizitem Cancel doppelt gezählt

[JobService::Cancel](../../src/runtime/Kernel/Runtime.JobService.cpp):505 erhöht
`CancelledJobs`. `ReleaseSatisfiedDependencies` sammelt denselben bereits
abgebrochenen Pending-Job erneut (:819–822) und erhöht beide Zähler
`CancelledJobs` und `DependencyCancelledJobs` (:866–869).
Dadurch zeigt die Diagnose mehr Abbrüche und eine falsche Abbruchursache.

Nur neu propagierte Dependency-Abbrüche dort zählen; die genau einmalige
Finalisierung muss erhalten bleiben. Regression: einen direkt abgebrochenen
`AwaitingDependencies`-Job durch den nächsten Drain führen und exakte Zählerdeltas
prüfen. Vorhandene `>= 1`-Assertions erkennen den Fehler nicht. Statisch, nicht ausgeführt.

### SRC RUNTIME 013 README soll einen aktuellen Persistenzvertrag beschreiben

[Runtime-README](../../src/runtime/README.md):1790–1797 beschreibt Referenzobjekte
als transient ohne `StableId`; ihre Modulübersicht :86 und
[ReferenceScene](../../src/runtime/Scene/Runtime.ReferenceScene.cpp):84–86 enthalten
bereits eine dauerhafte ID. Das sind gegensätzliche Anleitungen für neue Caller.

Den überholten Absatz entfernen und die 2.112-zeilige README auf Zuständigkeiten,
Hauptabläufe und Verweise kürzen. Detailverträge einmal bei ihrem Owner halten;
Zeilenzahl allein ist kein Fehler. Links und sachliche Aussagen prüfen.
`runtime-subsystem-boundaries.md` ist dagegen ausdrücklich als **Legacy baseline**
markiert; seine alten Engine-Getter wurden nicht als aktueller Architekturfehler gewertet.

### SRC RUNTIME 014 Dateinamen sollen den tatsächlichen Modulowner zeigen

[GeometryProcessingOperations.Normals.cpp](../../src/runtime/Editor/Operations/Runtime.NormalOperations.Processing.cpp):21
implementiert `NormalOperations`,
[GeometryProcessingOperations.Density.cpp](../../src/runtime/Editor/Operations/Runtime.PointFieldOperations.Processing.cpp):16
implementiert `PointFieldOperations`. Neue Familien wie PointSampling sind bereits passend benannt.

Bei einer ausdrücklich mechanischen Bereinigung Basenames an Owner angleichen,
Module/API unverändert lassen, CMake und Referenzen aktualisieren. Das verbessert
die Navigation ohne neue Targets oder Verzeichnisebenen. Symbol-/Dateisuche und
die passenden Build-/Strukturprüfungen sichern die Umbenennung ab.

## Was erhalten bleiben sollte

- Verantwortungsteilung in `Kernel`, `Scene`, `Config`, `AssetWorkflow`,
  `Rendering`, `GeometryIntegration`, `Editor/Operations`, `Modules/<feature>`
  und fokussierte Adapter. Kein großflächiger Ordnerumbau aus diesem Audit ableitbar.
- Schmaler Engine-Kernel, app-komponierte Module und Serviceauflösung mit mehreren
  echten Verbrauchern. Eine pauschale Löschung dieser Interfaces wäre unbegründet.
- Ein gemeinsamer CPU-Scheduler, verzögerte Commands/Events, Callbacks außerhalb
  von Locks, typisierte World-/Job-Handles, umgekehrte Modul-Teardown-Reihenfolge
  und `CancelAndDrain` vor Zerstörung.
- Gemeinsamer GPU-Transaktionslebenszyklus mit World-/Ring-Generationen,
  Cancellation und genau einmaliger Zustellung. Die vorhandenen Tests
  `Test.GpuTransactionLifecycleContract.cpp`, `Test.GpuPositionsAccept.cpp` und
  `Test.PointScalarTransaction.cpp` bilden wertvolle Grenzen.
- `.cppm`-/Implementierungstrennung und klare Ownership sind überwiegend
  konsistent. Unterschiede in Formatierung und Dateigröße allein rechtfertigen
  keinen Umbau; mehrfach gepflegte fachliche Regeln schon.

## Bereits bekannte offene Arbeit

| Bestehender Task | Stand beim Audit und Abgrenzung |
| --- | --- |
| [RUNTIME-274](../../tasks/active/RUNTIME-274-point-cloud-subsampling-workflow.md) | Sampling existiert. Weitere Voxel-/Octree-Strategien, progressive Prefix-Anzeige und Integrationsabdeckung sind offen. Erklärt nicht die neuen Fehler 001/002. |
| [RUNTIME-276](../../tasks/done/RUNTIME-276-declarative-config-field-specs.md) | Bestehender Owner der unvollständigen Feldtabellen-/Schemamigration, siehe 009. |
| [RUNTIME-294](../../tasks/active/RUNTIME-294-migrate-gpu-methods-to-property-residency.md) | Verbleibende GPU-Residency-Migrationen sind in Methoden-Slices 301–310 aufgeteilt. Nicht jede ursprüngliche Tabellenzeile ist weiterhin unimplementiert. |
| [RUNTIME-311](../../tasks/backlog/runtime/RUNTIME-311-unify-gpu-scalar-outlier-transaction-lifecycle.md) und [RUNTIME-314](../../tasks/backlog/runtime/RUNTIME-314-reuse-existing-processing-helpers.md) | Bereits gelandete Konsolidierungen und noch offene GPU-Verifikation unterscheiden. Alte Copy-paste-Befunde nicht ungeprüft erneut anlegen. |

## Verifikation und Reproduktion

### Tatsächlich ausgeführte Prüfungen

Die folgenden Prüfungen liefen während des Audits, bevor dieser Bericht erstellt wurde:

```bash
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_source_rules.py --root . --suite RuntimeEngineLayering --self-check
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests --parallel 4
ctest --test-dir build/ci --output-on-failure --timeout 60 \
  -R '^(PointSamplingOperations|AgentOperations|SceneDocumentModule|OutlierAnalysisConfig|SandboxAgentServer)\.'
```

| Prüfung | Tatsächliches Ergebnis |
| --- | --- |
| Layering | 960 Dateien, 10.266 Import-/Include-Referenzen, 89 CMake-Link-Kanten; keine Verletzung, keine Allowlist-Ausnahme |
| Runtime-Strukturregeln | 165 Regeln, 0 Probleme, einschließlich Mutation-Selbstcheck |
| Beide Test-Targets | Erfolgreich neu gebaut; bestehender `ci`-Build mit Clang 23 und passendem `clang-scan-deps`, Null/headless |
| Bestehende ausgewählte Tests | 76 bestanden: 51 Contract- und 25 Integrationstests |
| Zusätzliche Diagnosefälle | Fünf ausgeführt, alle fünf schlugen mit dem erwarteten Fehlerbild gegen unveränderte Production-Quellen fehl; belegen vier Defekte |

Zusätzliche Assertions wurden vor ihrer ersten Ausführung geschrieben. Sie
forderten korrektes Verhalten, nicht das Fortbestehen des Fehlers. Testquellen
wurden außerhalb des Repository-Quellbaums um diese Fälle ergänzt, mit den
Compile-Commands des bestehenden Targets gebaut und in eine separate Testbinary
gegen die frisch gebauten Objekte gelinkt. Die normalen Testobjekte wurden nicht
ersetzt. Es wurde kein Fix implementiert und folglich kein Grünlauf nach Fix behauptet.

### Lokales Übergabepaket

Die Ergebnissummary, die vollständige Ausgabe der fünf Diagnosefälle und der
Diagnosepatch liegen im Repository unter:

```text
tasks/evidence/SRC-RUNTIME-AUDIT-2026-10-16/runtime-audit-evidence.txt
tasks/evidence/SRC-RUNTIME-AUDIT-2026-10-16/runtime-audit-regressions.patch
```

Dieser Ordner ist gemäß Repository-Policy ignorierte, lokale Übergabeevidenz;
nicht force-addieren. Er ist für die folgende Bearbeitung auf diesem Checkout
vorhanden, aber nicht automatisch Teil eines anderen Clones. Die obigen
Reproduktionsabläufe und Ergebnissummaries bleiben unabhängig davon lesbar.
Der Patch ergänzt drei vorhandene Testdateien und repariert keinen Fehler.

Für die Folgeprüfung in einem eigenen sauberen Checkout auf dem genannten Stand
den Patch prüfen und anwenden, dann den normalen Target verwenden:

```bash
# AUDIT_PATCH auf die lokale Kopie des oben genannten Diagnosepatches setzen.
git apply --check "$AUDIT_PATCH"
git apply "$AUDIT_PATCH"
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^RuntimeAudit.*\.'
```

Diese Wiederholungsbefehle sind eine Anleitung, kein weiterer im Audit ausgeführter
Lauf. Auf dem Audit-Stand werden fünf fehlgeschlagene Tests erwartet. Für endgültige
Regressionstests Testnamen/Suiten den jeweiligen Ownern zuordnen und die nötigen
Negativkontrollen aus den Befunden ergänzen.

### CI Kontext

Der Default-Branch war bereits vor dem Audit rot:

- [ci-linux-clang 38869189381](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38869189381)
  auf dem Audit-Commit: Build erfolgreich, dann
  `RenderWorldPoolEngineWiring.SourceRules` ohne registrierten Producer bei
  `Capture CPU test selection`. Der nachfolgend fehlende `test.json`-Report ist
  ein Folgefehler. Bestehender Owner:
  [BUG-244](../../tasks/done/BUG-244-cpu-selection-rejects-source-rules-producer.md).
- [nightly-deep 38854737500](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38854737500)
  auf älterem Commit `4a5eede778b0830787fa139156f468c7ecf553e4`: vorherige 5.817
  CPU- und 62 Slow-Tests grün; Benchmark-Sealer lehnt
  `cpu_reference_sparse_cholesky` und `cpu_admm_sparse_cholesky` ab.
  Bestehender Owner: [BUG-245](../../tasks/done/BUG-245-nightly-smoke-backend-manifest-mismatch.md).
- `ci-docs` auf dem Audit-Commit war grün. Die CI-Fehler sind keine Belege für
  numerische Runtime-Fehler. Im Audit wurde kein Benchmark gestartet.

Die genannten BUG-Tasks waren bei der Berichtserstellung bereits im gemeinsam
genutzten Arbeitsbaum vorhanden; dieser Bericht legt sie nicht erneut an.

## Unabhängige Perspektiven und Grenzen

| Reviewerrolle | Schwerpunkt | Ergebnisquelle |
| --- | --- | --- |
| `/root/runtime_editor` | Nutzerablauf, Property-Publication, History, Save/Load | Befunde 001–004; zusätzlicher Gegencheck 005 und 007 |
| `/root/runtime_lifecycle` | Engine-Einbettung, Jobs, World-/Render-/Physics-Lifetime | Befunde 005–006 und 010–012 |
| `/root/runtime_maintainer` | Feature-Autorschaft, Config, Organisation, Dokumentation | Befunde 008–009, 013–014; zusätzlicher Gegencheck 006 |
| Koordinierender Codex-Agent | Fundstellenabgleich, Agent-API, Builds, deterministische Reproduktionen | 76 bestehende Tests und fünf zusätzliche Diagnosefälle |

Die Subagenten arbeiteten read-only und ohne weitere Delegation; diese
Beschränkung war instruction-only. Sie führten selbst keine Builds oder Tests
aus. Alle Reviewer bezogen sich auf denselben Commit. Das genaue geerbte Modell
und Reasoning-Level der Audit-Subagenten wurde nicht unabhängig berichtet.

Nicht geprüft wurden Live-Sandbox, Vulkan-Ausführung, ASan/UBSan, vollständige
CPU-Suite und eine Volllektüre aller Runtime-Dateien. Besonders 005–007 und
010–012 benötigen vor einer Reparatur einen gezielten reproduzierenden Test.
Aus dem Audit folgt keine Performance-Aussage und kein quantifizierter LOC-Gewinn.

## Empfohlene Bearbeitung durch die Folgeagenten

1. **Fragen 4 und 1:** 001, 002 und 003 in getrennten kleinen Fixes bearbeiten;
   Regression vorher rot, danach grün. History-Schutz in 002 nicht durch Löschen
   aller Versionsprüfungen umgehen.
2. **Fragen 4 und 3:** 004 und 008 reparieren; gemeinsame Validierungs- und
   Persistenzregeln an den schon vorhandenen Ownern absichern.
3. **Frage 4:** 005, 006 und 007 reproduzieren, dann die Adapter-/Grenzverträge
   korrigieren. Jeweilige ECS-/Physics-/Graphics-/Agent-Audits auf Überschneidungen prüfen.
4. **Fragen 1 bis 3:** RUNTIME-276 weiterführen und 010–011 als explizite
   API-Vertragsentscheidungen behandeln. 012 gezielt in den Diagnosetests absichern.
5. **Fragen 1 bis 3:** README und Modul-Basenames lokal bereinigen, getrennt von
   Semantikänderungen. Die vorhandene Verzeichnisstruktur zunächst beibehalten.

Bei der Gesamtsynthese anderer `src`-Berichte diesen Bericht als
`src/runtime`, Audit-Stand `66da33fd6`, referenzieren und die stabilen Befund-IDs
übernehmen. Bereits behobene oder überlappende Punkte mit Commit-/Taskbezug
kennzeichnen, statt daraus erneut unabhängige Arbeiten zu erzeugen.
