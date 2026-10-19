# Physics Audit und Übergabe

## Fragen und Auftrag

Die Fragen des Nutzers, unverändert:

1. „sollten wir physics aufräumen?“
2. „anders organisieren?“
3. „hat die implementierung einen durchgenden stil?“
4. „gibt es offene punkte, bugs, implementierungs gaps?“

Zusätzlicher Auftrag: „lasse subagenten aus verschiedenen "use-case rollen" und perspektiven darauf schauen.“ Dieser Bericht bewahrt die Ergebnisse für die anschließende gemeinsame Auswertung der Audits aller Unterordner von `src/` durch Codex 6 Astra Ultra. Er ist ohne den Chat lesbar.

Gemeinsamer Einstieg: [Berichte zu src](../reviews/src-audits.md).

## Ergebnis und Stand

**Gezielt aufräumen, zuerst Verhalten korrigieren.** Die bestehenden Layer und das eine öffentliche Modul sind sinnvoll. Die wichtigsten Probleme betreffen Kontaktkorrektur, Runtime-Transformationen, die Abbildung von ECS-Einstellungen, Schlafzustände und Fehlerdiagnostik. Eine neue Backend-, Service- oder Bridge-Abstraktion würde diese Fehler nicht beheben.

| Merkmal | Stand |
| --- | --- |
| Datum | 2026-10-16, Europe/Berlin |
| Repository | IntrinsicEngine |
| Auditiertes HEAD | `66da33fd6bfabb20408607b8361354fc9adb420d` |
| Primärumfang | `src/physics/Physics.World.cppm`, `Physics.World.cpp`, `CMakeLists.txt` vollständig |
| Größe | 351 Zeilen Interface, 1080 Zeilen Implementierung, 19 Zeilen CMake |
| Angenommene Nutzung | Optionale CPU-Simulation kleiner Szenen; keine gemessene Last- oder Echtzeitgarantie |
| Ergebnisstatus | Offene Auditbefunde; keine Fehlerkorrektur implementiert |
| Lokale Verifikation | Drei Testprogramme gebaut; 26 bestehende Tests bestanden; acht zusätzliche Sollverhalten-Proben fehlgeschlagen; strikte Layer-Prüfung bestanden |
| Arbeitsbaum | Auditierte Produktquellen entsprechen HEAD. Parallel entstandene Task-/CI-Notizen gehören zu anderen Arbeiten. |
| Zielbezug | Nutzerbeauftragte Prüfung der Buildbarkeit, Testbarkeit, Zuständigkeiten und Wartbarkeit gemäß AGENTS-Mission; keine neue Featurepriorisierung gegenüber dem bestehenden Framework24-P0 |

Die IDs `PHYS-AUDIT-01` bis `PHYS-AUDIT-15` sind stabile **Berichtsreferenzen**, keine angelegten Repository-Tasks. Alle sind am Auditstand offen. P1 bedeutet hoher Einfluss auf normales Simulationsverhalten; P2 bedeutet konkrete Korrektheits-/Vertragslücke; P3 bedeutet begrenzte Wartungsarbeit. Priorität und Evidenzklasse sind getrennt.

`Reproduziert` bezeichnet eine ausgeführte Assertion gegen die unveränderte, frisch gebaute Physics-Bibliothek. `Quellbelegt` bezeichnet einen vollständig verfolgten Codepfad ohne neue End-to-End-Probe. `Teilweise reproduziert` trennt diese Fälle im Befund ausdrücklich. Ein fehlgeschlagener Regressionstest ist hier ein Fehlernachweis, kein bestandener Implementierungscheck.

## Orientierung für die Gesamtauswertung

| Zuständigkeit | Einstieg |
| --- | --- |
| World, Handles, Integration, Kontakte, Inseln, Schlaf | [Physics.World.cppm](../../src/physics/Physics.World.cppm), [Physics.World.cpp](../../src/physics/Physics.World.cpp) |
| ECS nach Physics, Scheduling, Writeback, Config | [Runtime.PhysicsModule.cpp](../../src/runtime/Modules/PhysicsIntegration/Runtime.PhysicsModule.cpp) |
| Autorensicht auf Collider und Körper | [ECS.Component.Collider.cppm](../../src/ecs/Components/ECS.Component.Collider.cppm), [ECS.Component.RigidBody.cppm](../../src/ecs/Components/ECS.Component.RigidBody.cppm) |
| Lokale und Welttransformationen | [ECS.System.TransformHierarchy.cpp](../../src/ecs/Systems/ECS.System.TransformHierarchy.cpp), [ECS.Component.Transform.Local.cpp](../../src/ecs/Components/ECS.Component.Transform.Local.cpp) |
| World-Tests | [Test.Physics.World.cpp](../../tests/unit/physics/Test.Physics.World.cpp) |
| Vergleich mit CPU-Referenz | [Test.PhysicsSolverParity.cpp](../../tests/unit/physics/Test.PhysicsSolverParity.cpp), [rigid_body_reference](../../methods/physics/rigid_body_reference/README.md) |
| Runtime-Tests | [Test.PhysicsModule.cpp](../../tests/integration/runtime/Test.PhysicsModule.cpp) |
| Architekturvertrag | [Physics](../architecture/physics.md), [ADR 0019](../adr/0019-physics-layer-ownership-and-ecs-integration.md), [AGENTS.md](../../AGENTS.md) |

Für benachbarte Audits: Befunde 02, 03, 09, 10 und 12 gehören primär zur Runtime-Bridge. Befund 04 hat Physics- und Runtime-Anteile, Befund 05 benötigt Diagnostik über beide Schichten. Gleiche Symptome in einem Runtime-/ECS-Audit nicht doppelt zählen, sondern über diese IDs zusammenführen. ECS darf weiterhin Autorendeskriptoren enthalten, ohne Live-Physics-Zustand zu besitzen.

Bereits abgeglichene Überschneidung: `PHYS-AUDIT-02` entspricht `SRC-RUNTIME-005` im [Runtime-Audit](../reviews/2026-10-16-src-runtime-audit.md). Beide sind quellbelegt; Physics bewertet ihn als P1, Runtime als P2. Das sind unterschiedliche Priorisierungen desselben Fehlers, keine zwei Defekte. Die Gesamtauswertung entscheidet die Priorität anhand der tatsächlich benötigten Hierarchieszenarien.

## Befundübersicht

| ID | Priorität | Evidenz | Thema | Primärer Besitzer |
| --- | --- | --- | --- | --- |
| PHYS-AUDIT-01 | P1 | Reproduziert | Positionskorrektur überschießt | Physics, Referenzabgleich |
| PHYS-AUDIT-02 | P1 | Quellbelegt | Lokale Pose als Weltpose | Runtime |
| PHYS-AUDIT-03 | P1 | Quellbelegt | Authoring-Einstellungen still verworfen | Runtime, Physics |
| PHYS-AUDIT-04 | P1 | Teilweise reproduziert | Fehlende Aufweckbedingungen | Physics, Runtime |
| PHYS-AUDIT-05 | P1 | Quellbelegt | Kollisionsablehnung im operativen Pfad unsichtbar | Physics, Runtime |
| PHYS-AUDIT-06 | P2 | Reproduziert | Nichtendlicher Zustand trotz Erfolg oder Degraded | Physics |
| PHYS-AUDIT-07 | P2 | Reproduziert | Clear reaktiviert alte Handles | Physics |
| PHYS-AUDIT-08 | P2 | Reproduziert | Skalierung und Kindrotation falsch kombiniert | Physics |
| PHYS-AUDIT-09 | P2 | Quellbelegt | Authoring-Änderung setzt Geschwindigkeit zurück | Runtime |
| PHYS-AUDIT-10 | P2 | Quellbelegt | Kinematischer Collider driftet vom ECS-Objekt | Runtime, Physics-Vertrag |
| PHYS-AUDIT-11 | P2 | Reproduziert | Statische Kontakte erzeugen falsche Nichtkonvergenz | Physics |
| PHYS-AUDIT-12 | P2 | Quellbelegt | Fehlender Transform erzeugt Körper am Ursprung | Runtime |
| PHYS-AUDIT-13 | P2 | Quellbelegt | Mutable Getter umgeht Validierung und Wake | Physics-API |
| PHYS-AUDIT-14 | P3 | Quellbelegt | Doppelte Integrationslogik | Physics |
| PHYS-AUDIT-15 | P3 | Quellbelegt | Kleine tote Felder und Dokumentationsabweichungen | Physics, Tests, Dokumentation |

## Befunde mit Reproduktion und Reparaturansatz

### PHYS-AUDIT-01 Positionskorrektur überschießt

**Ort:** `src/physics/Physics.World.cpp:956–969`; Tests `Test.Physics.World.cpp:398–423,548–553`.

Die iterative Positionskorrektur verwendet in jedem Durchlauf dieselbe vor der Lösung erfasste Eindringtiefe. Zwei dynamische Kugeln mit Radius `0.5`, Zentren bei `x=0` und `x=0.8`, Nullgravitation und Standard-Solvereinstellungen enden bei einem Abstand von **2.07359982** statt ungefähr `1`. `Converged` erkennt nur die verschwundene Überlappung, nicht die Überkorrektur. Mehr Iterationen vergrößern den Fehler.

**Kleinster Reparaturansatz:** Die aktuelle Kontaktgeometrie während der Iterationen berücksichtigen. Die Referenzimplementierung unter `methods/physics/rigid_body_reference` und den Paritätstest gemeinsam prüfen: Beide verwenden denselben Ansatz; bloße Gleichheit mit der Referenz beweist hier keine Korrektheit.

**Regression:** Default-Einstellungen, obere und untere Grenze des Endabstands, Erhalt des Schwerpunkts sowie mehrere Iterationsbudgets. Der aktuelle Ruhetest setzt ausdrücklich `MaxIterations=2`, damit die Kugel nicht „catapulted“ wird; seine Energieassertion begrenzt die Positionsabweichung nicht. Ohne Korrektur bleiben unnatürliche Trennbewegungen trotz grüner Tests möglich. **Frage 4**, Testqualität auch Frage 1.

### PHYS-AUDIT-02 Lokale Transformation wird als Welttransformation verwendet

**Ort:** `Runtime.PhysicsModule.cpp:342–349,678–681,930–936`; Vergleich `ECS.System.TransformHierarchy.cpp:40`.

`TryBuildDescriptor` übernimmt `Transform::Component` direkt. Die Hierarchie berechnet die Weltmatrix dagegen aus Parentmatrix mal lokaler Matrix. Zwei Kinder mit gleicher lokaler Position unter weit auseinanderliegenden Parents erhalten dieselbe Physics-Position. Der Writeback schreibt die simulierte Pose wieder unmittelbar in lokale Position/Rotation; Parentrotation und -skalierung beeinflussen damit auch die sichtbare Simulationsbewegung falsch.

**Kleinster Reparaturansatz:** Runtime synchronisiert eine aktuelle aufgelöste Weltpose und rechnet den Writeback in die Parent-Koordinaten zurück. Den vorhandenen `Transform::TryComputeLocalTransform`-Pfad prüfen und wiederverwenden. Aktualisierungsreihenfolge der Weltmatrizen und die Behandlung nicht darstellbarer TRS/Scherungen gehören zum selben Fix.

**Regression:** Getrennte Parents mit identischen lokalen Collidern, Parentrotation, nicht uniforme Parentskalierung und Welt-Schwerkraft. Ohne Fix stimmen sichtbare Szene und Kollisionswelt bei Hierarchien nicht überein. **Frage 4**, Organisation Frage 2: Reparatur bleibt Runtime-Aufgabe.

### PHYS-AUDIT-03 Authoring-Einstellungen werden angenommen und verworfen

**Ort:** `Runtime.PhysicsModule.cpp:361–387,688–701`; deklarierte Felder `ECS.Component.Collider.cppm:40–58,82–88` und `ECS.Component.RigidBody.cppm:48–67`.

`ConvertShape` übernimmt Geometrie, Pose, Enabled und Trigger, aber weder Kollisionsfilter noch Material oder Kontakt-/Ruheabstände. `CollidesWithBits=0` verhindert deshalb keine Kontakte. Auch Dichte-Massenpolitik, lokaler Schwerpunkt, `AllowSleep`, `StartsAwake` und `ContinuousCollisionDetection` werden nicht umgesetzt. Bei `MassPolicy::Density` wird weiterhin `Mass.Mass` verwendet. Die verworfenen Felder erreichen den Descriptor-Fingerprint nicht; ihre Änderung bewirkt keine passende Synchronisation.

**Kleinster Reparaturansatz:** An der Runtime-Grenze den tatsächlich unterstützten Teilvertrag prüfen und unsupported Einstellungen sichtbar melden. Benötigte Kollisionsfilter durchgängig bis vor die Kontaktbildung transportieren. Weitere Funktionen einzeln umsetzen, statt allein wegen vorhandener ECS-Felder vorsorglich den Solver zu erweitern. Gewöhnliche Default-Materialwerte und eine neue Unsupported-Policy dürfen nicht versehentlich jede bisherige Szene ablehnen.

**Regression:** Verbotene Maskenkombination erzeugt weder Kontakt noch Trennbewegung, normale Maske als Positivkontrolle. Für jede weitere unterstützte Option einen Verhaltenstest, andernfalls eine klare Unsupported-Diagnose. Der Fehler ist die stille semantische Abweichung im operativen Pfad; die breitere ECS-Authoring-Struktur allein ist kein Bug. **Frage 4**.

### PHYS-AUDIT-04 Schlafende Körper reagieren nicht auf relevante Änderungen

**Ort:** `Physics.World.cpp:833–834,869–893,919–920,1037–1044`; Runtime-Config `Runtime.PhysicsModule.cpp:1144–1149`.

Zwei Fälle sind ausgeführt: Ein bewegter kinematischer Kontaktpartner weckt einen schlafenden dynamischen Körper nicht; `EnableSleep=false` hebt einen vorhandenen Schlafzustand ebenfalls nicht auf. Wake-Propagation betrachtet nur dynamische Inselmitglieder, und die Integration überspringt Schlafende vor den relevanten Einstellungen. Zusätzlich zeigt der Code: Eine globale Gravity-Änderung setzt nur den Runtime-Akkumulator zurück; ein unveränderter Body-Fingerprint löst kein Wake aus. Auch die Entfernung einer statischen Stütze hat keinen entsprechenden Wake-Pfad; dieser Fall wurde nicht ausgeführt.

**Kleinster Reparaturansatz:** Sleep-Deaktivierung vor der Integration anwenden; bewegte/geänderte Kontaktanker und tatsächlich geänderte globale Kräfte als Wake-Gründe berücksichtigen. Für die Runtime-Gravity-Änderung existiert bereits `World::WakeBody`.

**Regression:** Kinematischer Stoß, Sleep-Deaktivierung, Gravity-Hot-Apply und Entfernung einer Stütze. Die ausgeführten Proben setzen `TimeToSleepSeconds=0`; dieser Wert ist gültig und verkürzt nur die Vorbereitung. Beim Stoß hätte die normale Impulsantwort eine Geschwindigkeit über dem Schlafschwellwert ergeben, und auch `WakeTransitions` blieb null. Ohne Fix bleiben Körper trotz äußerer Einwirkung eingefroren. **Frage 4**.

### PHYS-AUDIT-05 Kollisionsablehnung verschwindet im Simulationspfad

**Ort:** `Physics.World.cpp:111–116,858–867`; `Runtime.PhysicsModule.cpp:981–1003`.

Ein dynamischer Body mit `Scale=(1,2,1)` besteht die Descriptor-Validierung. `BuildCollisionShape` verwirft seine Shapes und erhöht `DynamicNonUniformScaleRejects`. `SolveStep` übernimmt diese CollisionDiagnostics nicht, und der öffentliche PhysicsModule-Snapshot erhält sie ebenfalls nicht. Der Körper kann ohne Kollision fallen, während Runtime `Valid/Converged` sieht.

**Kleinster Reparaturansatz:** Nicht unterstützte Konfiguration möglichst vor Aufnahme in die Simulation ablehnen; Kollisionsablehnungen zusätzlich über die Solver- und Runtime-Diagnostik sichtbar machen. Das dokumentierte Verbot dynamischer nicht uniformer Skalierung bleibt bestehen.

**Regression:** Body über einem Boden durch den echten Runtime-Modulpfad anlegen; explizite Ablehnung beziehungsweise Unsupported-Diagnose erwarten. Kein still erfolgreicher kontaktloser Pfad. **Frage 4**.

### PHYS-AUDIT-06 Nichtendlicher Zustand wird nicht abgefangen oder zurückgesetzt

**Ort:** `Physics.World.cpp:847–851,896–900,972–976`; zugesagter Vertrag `Physics.World.cppm:230–232`.

Zwei ausgeführte Fälle: (a) endliche maximale Float-Gravitation, `GravityScale=2` und `dt=1` erzeugen eine nichtendliche Position, trotzdem meldet `SolveStep` `Converged`; (b) akzeptierte Masse `1e-40` und überlappender statischer Kontakt erzeugen über unendliche inverse Masse eine nichtendliche Position. Hier meldet der Solver `Degraded`, behält den beschädigten Zustand aber bei. Der öffentliche Kommentar verspricht dagegen den letzten endlichen Inselzustand.

**Kleinster Reparaturansatz:** Abgeleitete numerische Größen sowie Integrations-/Kontaktänderungen vor Übernahme prüfen; ungültige Änderungen verwerfen oder auf einen gültigen Zustand zurücksetzen und die Diagnose erhalten. Die gemeinsame Integration aus Befund 14 kann doppelte Absicherung vermeiden.

**Regression:** Beide Wege müssen einen endlichen gespeicherten Zustand erhalten und den Fehler sichtbar melden. Es sind extreme, von der öffentlichen World-API akzeptierte Werte; die enorme Gravitation passiert nicht die engere Runtime-Config-Validierung. Kein Nachweis für häufigen Overflow im normalen Sandbox-Betrieb. **Frage 4**.

### PHYS-AUDIT-07 Clear reaktiviert alte Handles

**Ort:** `Physics.World.cpp:1074–1078`, Slotgeneration `Physics.World.cppm:338`.

`AddBody → Clear → AddBody` erzeugt zweimal `{Index=0, Generation=1}`. Die Probe bestätigt, dass `Contains(oldHandle)` wieder wahr wird. Damit kann ein alter Handle einen anderen, neu erzeugten Körper verändern oder löschen.

**Kleinster Reparaturansatz:** Generationen beim Leeren erhalten und erhöhen; vorhandene Slots invalidieren und zur Wiederverwendung freigeben, statt ihre Identitätsgeschichte zu löschen.

**Regression:** Clear→Reuse zusätzlich zu bestehendem Destroy→Reuse; alte Handles dürfen weder lesen, aktualisieren noch zerstören. **Frage 4**.

### PHYS-AUDIT-08 Nicht uniforme Skalierung und Kindrotation werden falsch zusammengesetzt

**Ort:** `Physics.World.cpp:111,125–145`.

Skalen werden komponentenweise multipliziert und Rotationen separat zusammengesetzt. Diese Operationen sind nicht allgemein vertauschbar. Eine statische Box mit Halbausdehnung `(1,.25,.25)`, Körperskalierung `(2,1,1)` und Kindrotation `90°` um Z benötigt Welt-Halbausdehnung `(.5,1,.25)`; der Code erzeugt `(.25,2,.25)`. Die ausgeführte Probe findet bei einer kleinen Kugel an `(.4,0,0)` fälschlich keinen Kontakt und an `(0,1.5,0)` fälschlich einen Kontakt.

**Kleinster Reparaturansatz:** Für darstellbare Formen die transformierten Achsen korrekt bestimmen; Scherung beziehungsweise nicht unterstützte Kombinationen explizit ablehnen. Eine pauschale Ablehnung aller nicht uniformen Skalen wäre eine zusätzliche Einschränkung gegenüber aktuell unterstützten statischen Formen und muss bewusst bewertet werden.

**Regression:** Der ausgeführte orthogonale Fall ist ohne Scherung korrekt als OBB darstellbar. Dazu einen tatsächlich scherende Kombination testenden Ablehnungsfall ergänzen. Ohne Fix bleiben falsche positive und negative Kontakte möglich. **Frage 4**.

### PHYS-AUDIT-09 Authoring-Änderungen setzen simulierte Geschwindigkeit zurück

**Ort:** `Runtime.PhysicsModule.cpp:693–695,834–859`; `Physics.World.cpp:503`.

ECS enthält weiterhin die ursprünglich authorisierte Geschwindigkeit, während Physics die laufende Geschwindigkeit entwickelt. Eine Änderung allein an Masse, Dämpfung oder Collider baut den gesamten Descriptor neu auf; `UpdateBody` überschreibt dadurch auch die laufende Geschwindigkeit mit dem Initialwert. Der bisherige Masse-Update-Test führt den Edit ohne laufende Simulation aus und erkennt das nicht.

**Kleinster Reparaturansatz:** Bei Authoring-Änderungen den laufenden Zustand erhalten, soweit das zugehörige Authoring-Feld unverändert blieb. Die bisherige Gesamthash-Information reicht nicht aus, um diese Fälle zu unterscheiden; gezielt den benötigten vorherigen Authoring-Zustand vergleichen.

**Regression:** Fallenden Body beschleunigen, nur Masse oder Collidergröße ändern und Geschwindigkeitskontinuität prüfen; eine ausdrücklich geänderte Geschwindigkeit muss weiterhin angewendet werden. **Frage 4**.

### PHYS-AUDIT-10 Kinematischer Collider bewegt sich unabhängig vom sichtbaren Objekt

**Ort:** `Physics.World.cpp:841–844`; `Runtime.PhysicsModule.cpp:854,924–927`.

Der Solver integriert die Geschwindigkeit kinematischer Körper. Runtime überspringt deren Writeback absichtlich und synchronisiert eine unveränderte ECS-Pose wegen des Fingerprints nicht erneut. Ein sichtbares Objekt bleibt stehen, während sein Collider über mehrere Schritte weiterwandert. Bestehende Tests bestätigen nur die unveränderte ECS-Pose, nicht den tatsächlichen Kollisionsort.

**Kleinster Reparaturansatz:** Einen durchgängigen Pose-Eigentümer für Kinematik festlegen. Bei der dokumentierten externen Steuerung muss die ECS-Zielpose verbindlich sein; im Solver darf keine davon unabhängige dauerhafte Bewegung entstehen. Ein Wechsel zu simulationsgetriebener Kinematik wäre eine explizite Verhaltensentscheidung.

**Regression:** Kinematischer Körper mit Geschwindigkeit und dynamischem Kontaktpartner über mehrere Frames; sichtbare/authorisierte Pose und Kontaktort müssen übereinstimmen. **Frage 4**, Zuständigkeit auch Frage 2.

### PHYS-AUDIT-11 Statische Kontakte erzeugen falsche Nichtkonvergenz

**Ort:** `Physics.World.cpp:985–1025`; Runtime-Zähler `Runtime.PhysicsModule.cpp:1001–1002`.

Zwei überlappende statische Kugeln allein liefern `MaxIterationsReached`. Der Code zählt ihre Restüberlappung, obwohl `BuildIslands` solche Kontakte aus der dynamischen Lösung ausschließt. Es gibt keine dynamische Insel, die den Zustand korrigieren könnte. Überlappende Boden-/Wandcollider erzeugen dadurch irreführende Solverdiagnosen.

**Kleinster Reparaturansatz:** Konvergenz und Restfehler über die tatsächlich lösbaren dynamischen Kontakte bestimmen; die reine Kontaktabfrage darf statische Kontakte weiterhin liefern, wenn ihr Vertrag das vorsieht.

**Regression:** Statisch-statisch und statisch-kinematisch ohne dynamische Körper; kein falsches Ausschöpfen eines Iterationsbudgets. **Frage 4**.

### PHYS-AUDIT-12 Fehlender Transform erzeugt einen Körper am Ursprung

**Ort:** `Runtime.PhysicsModule.cpp:677–686,720–721,914–917`.

Fehlt `Transform::Component`, erhöht die Bridge `MissingTransforms`, setzt den Descriptoraufbau aber mit der Defaultpose fort. Der Körper kann am Ursprung Kontakte erzeugen, während der Writeback mangels Transform übersprungen wird.

**Kleinster Reparaturansatz:** Descriptoraufbau bei fehlendem Transform ablehnen und eine bereits vorhandene Bindung entfernen. Eine reine Diagnose ohne Verhinderung des aktiven Geisterkörpers reicht nicht.

**Regression:** Collider ohne Transform erzeugt keinen Body und keine Kontakte; Entfernen des Transforms von einem bestehenden Body entfernt dessen Bindung. **Frage 4**.

### PHYS-AUDIT-13 Mutable GetBody umgeht Validierung und Wake

**Ort:** `Physics.World.cppm:298–299`, `Physics.World.cpp:511–522`, Vergleich `UpdateBody:503–506`.

Der schreibbare Descriptor-Pointer erlaubt Mutationen ohne Validierung, Diagnostik oder Aufwecken. Beispielsweise kann ein Aufrufer die Geschwindigkeit eines schlafenden Körpers ändern, ohne dass dieser wieder integriert wird. Die Aufrufersuche im gesamten Repository fand außerhalb Physics nur lesende Zugriffe beziehungsweise Descriptor-Kopien; Runtime liest den Pointer als const.

**Kleinster Reparaturansatz:** Mutable Overload entfernen und den const-Getter behalten. Änderungen laufen durch das vorhandene `UpdateBody`; kein neuer Wrapper nötig. Vor Umsetzung die Aufrufersuche am dann aktuellen HEAD wiederholen.

**Regression:** Bestehende Aufrufer bauen weiterhin; bewusstes Descriptor-Update validiert und weckt. Geliehene Pointer behalten ihre bisherige durch die Vektorverwaltung begrenzte Lebensdauer; diese Änderung allein macht sie nicht stabil. **Fragen 1, 3 und 4**.

### PHYS-AUDIT-14 Integrationslogik ist doppelt vorhanden

**Ort:** `Physics.World.cpp:555–573` und `836–854`.

`Step` und `SolveStep` wiederholen den Switch für Kinematik, Gravitation, Dämpfung, Translation und Rotation. Numerische Absicherung oder spätere Integratoränderungen müssten in beiden Kopien erfolgen.

**Kleinster Reparaturansatz:** Eine private gemeinsame Integration pro Body verwenden. Unterschiedliche Verträge der öffentlichen Funktionen erhalten: `Step` ist der rohe Integrationspfad, `SolveStep` berücksichtigt Schlaf und Kontakte. Keine neue Strategy-, Service- oder öffentliche Modulschicht.

**Regression:** Bestehende Integrations- und Paritätstests vor und nach der mechanischen Konsolidierung; Sleep-Verhalten getrennt absichern. **Fragen 1 und 3**.

### PHYS-AUDIT-15 Kleine tote Felder und Dokumentationsabweichungen

**Orte:** `Physics.World.cpp:30,121`; `Physics.World.cppm:1`; `Test.Physics.World.cpp:308–312`; [Physics-Backlog](../../tasks/backlog/physics/README.md).

`BuiltCollisionShape::Motion` wird gesetzt, aber nicht gelesen. Der Moduldatei fehlt die vorgeschriebene kurze Zweckbeschreibung. Ein Testkommentar behandelt BUG-025 noch als offen, während die Architektur dessen Fix dokumentiert. Der Physics-Backlog-Index nennt 005–007, lässt die vorhandene PHYSICS-008 aus. Mehrere Kommentare erzählen Implementierungshistorie statt heutiger Verträge.

**Kleinster Reparaturansatz:** Ungelesenes Feld samt Zuweisung entfernen, veraltete Aussagen korrigieren, Synopsis und fehlenden Backlog-Link ergänzen. Wertvolle Ordering-, Sleep- und Fehlerverträge behalten. `IsValidUnitRotation` und `IsFinite(quat)` haben nach Aufrufersuche keine externen Verbraucher und könnten privat bleiben; `IsFinite(vec3)` wird dagegen von Runtime verwendet.

**Prüfung:** Source-Dokumentationsscanner meldete eine objektive Verletzung und vier manuell zu bewertende Kommentarhinweise. Diese Hinweise sind nicht vier weitere Bugs. Mit Getter-/Feldbereinigung sind mindestens neun Quellzeilen direkt entfernbar, ohne eine neue Abhängigkeit; das ist keine vermessene Gesamtersparnis. **Fragen 1 und 3**.

## Organisation und bewusst begrenzter Funktionsumfang

Die Ownership passt: Physics besitzt Welt und Solverzustand, Geometry die Kontakt-/Geometriekerne, Runtime die Verbindung zu ECS. `.cppm` enthält bereits weitgehend Records, Deklarationen und kleine Getter. PascalCase, `m_`-Member, Initialisierung und explizite Statuswerte sind überwiegend einheitlich. Die gravierenden Inkonsistenzen betreffen erfüllte Verträge und Fehlerbehandlung, nicht Formatierung.

Nach den Fehlerkorrekturen wäre Shape-Aufbau plus Dispatch plus `ComputeCollisionContacts` eine nachvollziehbare private Implementierungsdatei **desselben** Moduls. Kein neuer Ordner und kein öffentliches Collision-/Solver-Framework sind dafür erforderlich. Nur bei konkretem Wartungsgewinn trennen, nicht wegen einer Zeilenzahl. Mechanischen Dateisplit und semantische Reparaturen getrennt halten.

Bewusste Grenzen und Produkt-Gaps, nicht als zusätzliche neu entdeckte Bugs gezählt:

| Grenze | Bedeutung und Besitzer |
| --- | --- |
| All-Pairs-Broadphase | `ComputeCollisionContacts` prüft alle Formpaare und speichert Kandidaten; `SolveStep` führt Kontaktbildung einschließlich Residuen zweimal aus. Quadratischer Algorithmus ist quellbelegt, kein gemessener Performancebefund. [PHYSICS-008](../../tasks/backlog/physics/PHYSICS-008-spatial-broadphase-and-kdop-deformable-collision.md) besitzt den Ausbau. |
| Lineare Kontaktantwort | Keine Winkelträgheit/-impulse im Kontaktlöser, Energieangaben nur linear. Dokumentierter Referenzumfang. |
| Colliderumfang | Sphere, Capsule, Box/OBB; weitere Mesh-/Deformable-Collider sind nicht Teil des bestehenden ersten Vertrags. |
| Kontakt-Events | Runtime veröffentlicht noch keine Kontakt-Events für Editor/App; das ist dokumentiert. |
| Physics-Szenen speichern | [Runtime.SceneSerialization.cpp](../../src/runtime/Scene/Runtime.SceneSerialization.cpp), Zeilen 2734–2738, zählt Collider/RigidBody als `UnsupportedPhysicsEntities` und serialisiert deren Physics-Komponenten nicht. [Serializer-Test](../../tests/contract/runtime/Test.RuntimeSceneSerialization.cpp), Zeilen 431–470, erwartet diese Auslassung. Relevanter Produkt-Gap, aber kein unbemerkter Verstoß gegen den heutigen Serializervertrag. |
| Nicht starre Methoden | CPU-Referenzen unter `methods/physics` sind nicht gleichbedeutend mit Runtime-Integration. [PHYSICS-005](../../tasks/backlog/physics/PHYSICS-005-vulkan-sph-fluid.md), [PHYSICS-006](../../tasks/backlog/physics/PHYSICS-006-vulkan-particle-spring.md), [PHYSICS-007](../../tasks/backlog/physics/PHYSICS-007-vulkan-xpbd-cloth.md) und PHYSICS-008 enthalten Ausbauarbeit; nicht erneut als frische Auditaufgaben duplizieren. |

## Verifikation und auffindbare Belege

### Bestehende Tests und Layer-Prüfung

Diese Befehle wurden in der Auditsitzung ausgeführt, nicht lediglich vorgeschlagen:

```bash
cmake --build --preset ci --target IntrinsicPhysicsWorldTests IntrinsicPhysicsMethodTests IntrinsicRuntimePhysicsModuleTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(PhysicsWorld|PhysicsSolverParity|PhysicsModule)\.'
python3 tools/repo/check_layering.py --root src --strict
```

Build mit `/bin/clang++-23` und passendem `/bin/clang-scan-deps-23`; vorhandener `ci`-Buildbaum. **19 PhysicsWorld-, 2 PhysicsSolverParity- und 5 PhysicsModule-Tests bestanden, insgesamt 26.** Die PhysicsModule-Auswahl enthält den NullEngine-Integrationstest. Der Build von `IntrinsicPhysicsMethodTests` bedeutet nicht, dass alle Methoden-Suites ausgeführt wurden. Die Layer-Prüfung meldete keine Verstöße.

Die Maintainer-Rolle führte außerdem aus:

```bash
python3 tools/agents/skills/intrinsicengine-source-documentation/scripts/audit_source_documentation.py --root . --path src/physics --no-fail
```

Ergebnis: eine fehlende Synopsis, vier Review-Hinweise zu Kommentaren. `--no-fail` macht daraus keinen bestandenen Dokumentationsvertrag.

### Acht gezielte Sollverhalten-Proben

Temporäre Belege liegen lokal im Repository unter `tasks/evidence/PHYSICS-AUDIT-2026-10-16/`. Sie bleiben gemäß AGENTS §8b ignoriert und werden nicht force-added. Der Bericht enthält die entscheidenden Eingaben und Ergebnisse dauerhaft; die Rohdateien bleiben für die vom Nutzer angekündigte Gesamtauswertung erhalten. Das Evidence-Verzeichnis ist kein neuer aktiver Task und keine Implementierungsfreigabe.

| GTest unter PhysicsAudit | Erwartung vor Ausführung | Beobachtung | Befund |
| --- | --- | --- | --- |
| ClearInvalidatesOldHandles | Alter Handle nach Clear→Add ungültig | `Contains(old)` ist true | 07 |
| DisablingSleepResumesIntegration | `EnableSleep=false` weckt und integriert | Weiter asleep, 0 integrierte dynamische Bodies | 04 |
| KinematicContactWakesSleepingBody | Bewegter kinematischer Kontakt weckt | Weiter asleep, 0 WakeTransitions | 04 |
| StaticOverlapDoesNotFailDynamicSolver | Zwei statische überlappende Kugeln ergeben keinen dynamischen Solverfehler | `MaxIterationsReached` | 11 |
| ProjectionDoesNotOvershootSeparation | Radius .5 + .5, Abstand .8 wird höchstens 1.001 | Abstand 2.07359982 | 01 |
| IntegrationOverflowIsReportedAndPreservesFiniteState | Numerischer Fehler wird gemeldet, Zustand bleibt endlich | `Converged`, Position nicht endlich | 06 |
| DegradedContactSolvePreservesFiniteState | Bei Masse 1e-40 bleibt letzter endlicher Zustand erhalten | `Degraded`, Position nicht endlich | 06 |
| NonuniformParentScaleComposesWithRotatedChild | Kontakt bei x=.4, kein Kontakt bei y=1.5 | Genau umgekehrte Kontaktergebnisse | 08 |

Die ersten sieben Proben liefen gemeinsam, die achte nach Erweiterung separat. Beide Ausführungen endeten mit Exitcode 1, erwartungsgemäß für die offenen Defekte. Acht Szenarien bedeuten nicht acht unabhängig zu reparierende Ursachen. Es wurden keine Fixes oder Mutationsläufe nach einem Fix ausgeführt. Folgeimplementierungen müssen die bisherigen Fehlschläge in echte Regressionen überführen und den Vorher-/Nachher-Nachweis gemäß AGENTS liefern.

Dateien im Evidence-Verzeichnis:

- `PhysicsAudit.cpp`: vollständige acht GTest-Proben mit zuvor formulierten Sollassertionen.
- `physics-audit-probes.log`, `physics-audit-scale.log`: unveränderte Ausgaben der negativen Proben.
- `physics-build.log`, `physics-ctest.log`, `physics-layering.log`: bestehender Build und Prüfungen.
- `ci-failed.log`: abgerufener fehlgeschlagener Main-CI-Schritt.
- `SHA256SUMS`: Hashbindung der kopierten Belege.
- `recipe-replay.log`: erneute Ausführung aller acht Proben über das unten dokumentierte Rezept aus dem Repository; wieder acht fehlgeschlagene Sollassertionen, Exitcode 1.
- `report-reviewed.txt`, `report-maintainer-review.txt`, `report-maintainer-review.json`: eingefrorener Berichtsstand und tatsächliches unabhängiges Urteil.
- `report-review-packet.md`, `report-review-record.json`, `report-review.json`: zusätzlicher Claude-CLI-Aufruf; Timeout ohne Antwort oder Urteil, JSON-Ausgabedatei leer.

Die Proben wurden mit der `Test.Physics.World.cpp`-Compile-Command aus `build/ci/compile_commands.json` gebaut; nur Eingabedatei und Ausgabeobjekt wurden ersetzt. Der Linkbefehl von `ninja -C build/ci -t commands bin/IntrinsicPhysicsWorldTests` wurde mit entsprechend ersetztem Objekt/Binary wiederverwendet. So liefen die Proben gegen die Originalbibliothek und deren tatsächliche Modul-/Compilerkonfiguration, ohne Repository-Tests zu ändern.

Reproduktion am passenden Buildstand, ab Repositorywurzel und nach dem oben genannten fokussierten Build:

```bash
python3 - <<'PY'
import json
import shlex
import subprocess
from pathlib import Path

repo = Path.cwd()
build = repo / "build/ci"
evidence = repo / "tasks/evidence/PHYSICS-AUDIT-2026-10-16"
entry = next(item for item in json.loads((build / "compile_commands.json").read_text())
             if item["file"].endswith("/Test.Physics.World.cpp"))
compile_command = shlex.split(entry["command"])
compile_command[compile_command.index("-o") + 1] = str(evidence / "PhysicsAudit.o")
compile_command[compile_command.index("-c") + 1] = str(evidence / "PhysicsAudit.cpp")
subprocess.run(compile_command, cwd=build, check=True)
raw = subprocess.check_output(
    ["ninja", "-C", str(build), "-t", "commands", "bin/IntrinsicPhysicsWorldTests"],
    text=True).splitlines()[-1]
tokens = shlex.split(raw)
assert tokens[:2] == [":", "&&"] and tokens[-2:] == ["&&", ":"], raw
link_command = tokens[2:-2]
assert sum(part.endswith("/Test.Physics.World.cpp.o") for part in link_command) == 1
link_command = [str(evidence / "PhysicsAudit.o")
                if part.endswith("/Test.Physics.World.cpp.o") else part
                for part in link_command]
link_command[link_command.index("-o") + 1] = str(evidence / "PhysicsAudit")
subprocess.run(link_command, cwd=build, check=True)
raise SystemExit(subprocess.run(
    [str(evidence / "PhysicsAudit"), "--gtest_color=no"], cwd=build).returncode)
PY
```

Das Rezept ist an das beobachtete Ninja-Linkformat gebunden und prüft dieses ausdrücklich. Nach späteren Buildsystemänderungen neu ableiten, nicht einen alten Binarylauf als aktuelle Verifikation ausgeben. Kein Benchmark oder Ergebnisexperiment ist für diese deterministischen bekannten Sollfälle erforderlich.

### CI zum Auditbeginn

Am auditierten HEAD war [ci-docs](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38869189384) grün, [ci-linux-clang](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38869189381) rot. Der fehlgeschlagene Schritt `Capture CPU test selection` meldete:

> BLOCKED: selected CTest test 'RenderWorldPoolEngineWiring.SourceRules' does not map to a registered producer

Das spätere fehlende `test.json` ist eine Folge. Dies ist keine fehlgeschlagene Physics-Testassertion. Die parallel erfasste Notiz [BUG-244](../../tasks/done/BUG-244-cpu-selection-rejects-source-rules-producer.md) besitzt die Reparatur. Der ebenfalls rote [nightly-deep-Lauf](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38854737500) ist separat in [BUG-245](../../tasks/backlog/bugs/BUG-245-nightly-smoke-backend-manifest-mismatch.md) erfasst. Deren Ursachen wurden nicht im Physics-Audit repariert. CI-Zustände sind historische Beobachtungen dieses Stands, keine Aussage über spätere Runs.

## Rollen und Review

Alle drei Subagenten prüften unabhängig und rein lesend, ohne weitere Delegation:

| Rolle und lokale Sessionreferenz | Schwerpunkt | Beitrag |
| --- | --- | --- |
| `/root/simulation_user` | Nutzer von Simulation und Kollision | Vollständiges Physics-Modul, relevante Geometry-Kontaktpfade, Solver-/Paritätstests; numerische und Lifecycle-Fälle |
| `/root/scene_integrator` | Szenenautor und Runtime-Verbraucher | PhysicsModule, ECS-Deskriptoren, Hierarchie, Scene-Persistence und Integrationstests |
| `/root/maintainer_api` | API-Verbraucher, Maintainer und Testautor | Stil, Modulgrenzen, Aufrufersuchen, Doppelungen und Teststärke |

Der koordinierende Codex führte die Builds, 26 Bestandstests, acht Proben und die Layer-Prüfung aus. Die Rollen selbst behaupten keine eigenen C++-Testläufe. Die Maintainer-Rolle prüfte anschließend unabhängig Quelltext, Log und Implementierung der ersten sieben Proben. Ihr tatsächliches Urteil:

> Die sieben Proben tragen die beschriebenen lokalen Fehlerbehauptungen. Die Empfehlung „gezielte Fehlerkorrekturen vor optionalem privatem Dateisplit; Stil weitgehend einheitlich“ passt zu allen vier Nutzerfragen.

Sie bestätigte ausdrücklich, dass die numerischen Proben extreme akzeptierte World-Eingaben betreffen und nicht alle davon den Runtime-Config-Pfad passieren. Die achte Skalierungsprobe wurde vom Koordinator separat ausgeführt; sie war nicht Teil dieses Sieben-Proben-Urteils. Für diese Subagenten waren Lesemodus und Delegationsverbot instruktionell, nicht durch einen gesonderten Sandboxmechanismus erzwungen.

Unabhängiger Review des gespeicherten Berichts am 2026-10-16 durch `/root/maintainer_api`, gebunden an SHA-256 `bef2b1eddccef3d9e62d020432a5c4fd98e32155ed66650caa0642af85bcda87`. Der vollständige geprüfte Stand ist als `report-reviewed.txt` im Evidence-Verzeichnis erhalten; danach wurden nur diese Review-/Verifikationsangaben ergänzt. Tatsächliches Urteil: **„approve“** und **„Keine materiellen Ungenauigkeiten oder blockierenden Befunde.“** Der Reviewer bestätigte alle vier beantworteten Fragen, konsistente 15 IDs, Evidenzklassen, Testzählung, Grenzen der Proben und die eigenständig nutzbare Übergabe. Er las den vollständigen Bericht und die Belege und prüfte die sieben ursprünglichen Evidence-Dateihashes; keine C++-Tests wurden von ihm wiederholt. Modell/Effort wurden vom Elternagenten geerbt und nicht separat vom Tool ausgewiesen; Lesemodus/Delegationsverbot waren instruktionell.

Zusätzlich wurde nach dem `peer`-Skill Claude Code mit angefordertem Modell `claude-opus-5-5`, Effort `max`, ausschließlich `Read,Grep,Glob` und `--strict-mcp-config` auf denselben Bericht angesetzt. Der Prozess endete nach dem festgelegten 360-Sekunden-Limit mit Exitcode 124, ohne Antwort, Session-ID oder gemeldetes Modell. Lesemodus und Rekursionsschutz waren durch die Werkzeugbeschränkung erzwungen. **Dieser Aufruf liefert kein Urteil und gilt nicht als Zustimmung.** Packet und Timeout-Metadaten liegen im Evidence-Verzeichnis. Kein Berichtsurteil bedeutet eine Abnahme bereits korrigierter Implementierung.

Für die gespeicherte Dokumentation bestanden die strikten Dokumentlink-, Task-Policy- und Docs-Sync-Prüfungen. Docs-Sync wurde mit expliziter Liste dieses Berichts, `docs/reviews/src-audits.md` und `docs/index.md` ausgeführt, damit auch die noch nicht versionierte Datei erfasst ist. Das dokumentierte Reproduktionsrezept wurde zusätzlich aus dem Repository ausgeführt und bestätigte alle acht negativen Fälle. Die Engineimplementierung blieb unverändert.

## Empfohlene Arbeitsfolge für Codex 6 Astra Ultra

1. **Stand und Überschneidungen prüfen, Fragen 1–4.** HEAD und Diff der genannten Quellen mit dem Auditstand vergleichen; Befunde mit Runtime-/ECS-/Geometry-Berichten zusammenführen. CI-Defekte ihren vorhandenen BUG-Tasks zuordnen. Die Berichts-IDs beim Zusammenführen beibehalten.
2. **Solververhalten absichern, Frage 4.** Befunde 01, 04, 06, 07, 08 und 11 in kleine Reparaturblöcke mit jeweils vorher scheiternder Regression schneiden. Referenzparität bei 01 inhaltlich prüfen. Nicht sämtliche Solverfehler in einen Umbau bündeln.
3. **Runtime-Vertrag schließen, Fragen 2 und 4.** Befunde 02, 03, 05, 09, 10 und 12 über echte Integrationsfälle absichern. Unterstützte Authoring-Semantik, Welt-/Lokaltransformation und Kinematik-Eigentum ausdrücklich halten; neue Fähigkeiten nur bei tatsächlichem Bedarf ergänzen.
4. **API und Doppelungen reduzieren, Fragen 1 und 3.** Befunde 13–15 bearbeiten. Den gemeinsamen Integrator bei Bedarf mit der numerischen Absicherung verbinden; weitere mechanische Bereinigungen getrennt halten.
5. **Organisation nur bei verbleibendem Nutzen ändern, Frage 2.** Ein optionaler privater Collision-Dateisplit kann nach den Verhaltensfixes folgen. Öffentlichen Modulnamen und Ownership beibehalten. Keine Layer-Ausnahme oder neue Architektur aus diesem Bericht ableiten.

Done für die **Auswertung** bedeutet: alle vier Nutzerfragen beantwortet, jeder übernommene Befund mit Besitzer, Evidenz und passendem Test versehen, Überschneidungen dedupliziert. Done für eine **Reparatur** bedeutet zusätzlich: zugehörige Regression vor Fix fehlgeschlagen, nach Fix bestanden, betroffene vorhandene Tests bestanden und unabhängiger Review des konkreten Diffs. Dieser Bericht löst keine unaufgeforderte Implementierung aller Punkte aus.

Nicht untersucht wurden Performance unter realer Last, Sanitizer, GPU/Vulkan-Physics, die vollständigen nicht starren Methodenpakete und eine interaktive Sandbox-Szene. Vorhandene NullEngine-Integration ist kein Ersatz für diese Nachweise. Die Empfehlung lautet deshalb gezielte Korrektheit und kleinere Oberfläche, nicht eine Aussage über allgemeine Produktionsreife.
