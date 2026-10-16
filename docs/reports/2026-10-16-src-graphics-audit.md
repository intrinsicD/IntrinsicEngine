# Graphics Audit und Übergabe vom 16. Oktober 2026

## Auftrag und ursprüngliche Fragen

1. „sollten wir graphics aufräumen?“
2. „anders organisieren?“
3. „hat die implementierung einen durchgenden stil?“
4. „gibt es offene punkte, bugs, implementierungs gaps?“

Zusätzlich beauftragt: „nutze das ponytail plugin dafür“ und „lasse subagenten aus verschiedenen \"use-case rollen\" und perspektiven darauf schauen.“

Anschließender Auftrag: „Erstelle einen Bericht aus deinen Ergebnissen in IntrinsicEngine so dass Codex 6 Astra Ultra anschließend alles finden kann. Ich lasse für alle weiteren unterordner von IntrinsicEngine/src von anderen Agenten ebenfalls so einen Bericht erstellen“.

Dieser Bericht ist die eigenständig lesbare Übergabe für die bereichsübergreifende Auswertung. Der gemeinsame Einstieg ist [Berichte zu src](../reviews/src-audits.md). Die Prüfung dient [REVIEW-004](../../tasks/backlog/architecture/REVIEW-004-framework24-product-convergence-audit.md) und dem [Framework24-Konvergenzziel](../product/framework24-convergence.md): zuverlässige Uploads, Ressourcenlebenszyklen, Rendering und Property-Visualisierung tragen die bestehenden Nutzerabläufe. Konkrete Fehler in diesen Pfaden zu isolieren ist der kürzeste Beitrag zu diesem Ziel; dieses Audit ist kein Nachweis vollständiger Produktkonvergenz.

## Ergebnis und Gültigkeit

**Gezielt aufräumen, die Hauptschichten behalten.** Die dringenden Arbeiten betreffen Fehlerbehandlung und GPU-Lebensdauer. Eine neue Ordnerstruktur behebt diese Fehler nicht. Danach lohnen sich kleinere Verantwortungsbereiche innerhalb von `renderer` und das Zusammenführen tatsächlich gleicher Pipeline-Initialisierung. Der Modulstil ist überwiegend einheitlich, die Behandlung von Fehlerzuständen und die Genauigkeit der API-Verträge sind es noch nicht.

| Merkmal | Stand |
| --- | --- |
| Prüfdatum | 2026-10-16, Europe/Berlin |
| Geprüfter Quellstand | `66da33fd6bfabb20408607b8361354fc9adb420d`, `main` |
| Primärer Bereich | `src/graphics`; verbundene Runtime-Aufrufer, Tests, Build- und Aufgabenunterlagen gezielt verfolgt |
| Umfang | 217 C++-Quelldateien (`.cpp`, `.cppm`, `.hpp`), 74.451 Zeilen; risikoorientierte Tiefenprüfung, keine Volllektüre aller Zeilen |
| Quelländerungen | Keine; keine der unten beschriebenen Reparaturen umgesetzt |
| Bestehende Tests | Zwei Testprogramme gebaut; 264 ausgewählte CPU-CTests bestanden |
| Zusätzliche Nachweise | CPU-Reproduktion zweier Staging-Allocator-Fehler; zwei arithmetische Grenzproben |
| Nicht ausgeführt | Vulkan-/GPU-Integration, Sanitizer, gesamte CPU-Suite, Performance-Messungen |

Kennungen `SRC-GRAPHICS-001` bis `SRC-GRAPHICS-008` sind stabile Berichtsschlüssel, keine neu angelegten Task-IDs oder Implementierungsfreigaben. Prioritäten bewerten die mögliche Auswirkung, nicht eine gemessene Häufigkeit im Editor. Zeilennummern gelten für den genannten Commit; die Symbole sind die Suchanker bei späteren Verschiebungen. Vor einer Reparatur aktuellen Quellstand und Aufgabenbesitzer erneut prüfen.

## Perspektiven und Abdeckung

| Perspektive | Agent | Untersuchte Nutzung und Fragen |
| --- | --- | --- |
| Backend-Maintainer | `/root/backend_maintainer` | Uploads, Staging, native Lebensdauer, Synchronisation, Fehlerpfade; Fragen 1, 3, 4 |
| Renderer-Erweiterer | `/root/renderer_extender` | Neue Passes, Recipes, Frame-Ausführung, Verantwortungsgrenzen und Tests; Fragen 1–4 |
| Geometry-/Property-Nutzer | `/root/geometry_user` | Große Datenmengen, GPU-Methoden, Residency, Smoothing und Visualisierung; Fragen 1–4 |
| Koordination | Hauptagent | Zusammenführung, Aufruferprüfung, bestehende Aufgaben, Build/Tests und deterministische Proben |
| Unabhängiger Peer | Claude, separat und lesend | Staging, abgelehnte Uploads, Capture beim Minimieren und Contribution-Vertrag; Details im Review-Nachweis unten |

Die Rollenagenten erhielten einen lesenden Auftrag ohne weitere Delegation. Verwendet wurden Ponytail, Ponytail Audit und die einschlägigen Repository-Regeln. Der unabhängige Peer hat nur die vier ausdrücklich genannten Themen beurteilt, nicht sämtliche Aussagen dieses Berichts.

## Befundübersicht

| Kennung | Priorität | Befund | Belegstatus | Primärer Owner |
| --- | --- | --- | --- | --- |
| SRC-GRAPHICS-001 | P1 | Abgelehnter Upload kann als `Ready` veröffentlicht werden | Quellfluss, unabhängig bestätigt; kein GPU-Repro | graphics/assets + vulkan/Transfer |
| SRC-GRAPHICS-002 | P1 | Staging-Ring überschreibt belegte oder noch nicht eingereichte Bereiche | Zwei CPU-Reproduktionen extrahierter Originalmethoden; kein GPU-Repro | vulkan/Staging + Transfer |
| SRC-GRAPHICS-003 | P1 | Einzeltextur-Upload reserviert weniger Bytes als der Copy liest | Quellbefund und konkreter Größenfall | vulkan/Transfer + rhi/TextureUpload |
| SRC-GRAPHICS-004 | P1 | Native Löschung nach `EndFrame` landet im nächsten Frame-Slot | Backend-Lifecycle aus Quelle; aktueller Nutzerpfad nicht nachgewiesen | vulkan/Device |
| SRC-GRAPHICS-005 | P1 | Fehlgeschlagene Transient-Allokation bricht Frame-Ausführung nicht ab | Quellfluss; keine ausgeführte Fehler-Injektion | renderer/Renderer |
| SRC-GRAPHICS-006 | P2 | Akzeptierte Pass-Contributions werden beim Build teilweise ignoriert | Unabhängig bestätigte Erweiterungs-API-Lücke; Defaults nicht als defekt nachgewiesen | renderer/FrameRecipe |
| SRC-GRAPHICS-007 | P2 | Akzeptierte Smoothing-Größen überschreiten mögliche Dispatch-Grenze | Aufrufer und Quellfluss; arithmetische Probe | renderer/PropertyFilter + Runtime Smoothing |
| SRC-GRAPHICS-008 | P2 | Konstante große Scalar-Werte erzeugen weiterhin eine leere Range | Arithmetisch reproduziert; verbundener Runtime-Pfad statisch | runtime/Visualization + graphics/VisualizationSyncSystem |

### SRC-GRAPHICS-001 — Abgelehnter Upload wird Ready

**Auslöser und Folge.** Das Backend ist operational und die GPU-Ressource wird angelegt, aber der Transfer liefert einen ungültigen Token, etwa bei vollem Staging-Ring, zu großem Upload oder fehlgeschlagenem Begin/Submit. `GpuAssetCache` übernimmt den Token und meldet `Ok`. Vulkan behandelt einen ungültigen Token in `IsComplete` als abgeschlossen. `Tick` kann deshalb eine Ressource ohne hochgeladenen Inhalt als `Ready` veröffentlichen. Bei Reload wird zusätzlich der bisher gültige `Current` zum Retire vorgemerkt.

**Fundstellen.** [Graphics.GpuAssetCache.cpp](../../src/graphics/assets/Graphics.GpuAssetCache.cpp), Buffer-Upload 250–265, Texture-Upload 323–341, Fallback 512–525, Promotion 584–602; [Backends.Vulkan.Transfer.cpp](../../src/graphics/vulkan/Backends.Vulkan.Transfer.cpp), `IsComplete` 1073–1079. Die Standard-Staging-Kapazität in [Transfer.cppm](../../src/graphics/vulkan/Backends.Vulkan.Transfer.cppm):30 beträgt 64 MiB; ein zusammenhängender 8192×8192-RGBA8-Upload benötigt 256 MiB und ist ein konkreter Ablehnungsfall.

**Kleinste Korrektur.** Den Token vor Veröffentlichung eines Pending-Zustands prüfen, den bisherigen `Current` erhalten und Fehler oder bewusstes Deferral zurückgeben. Den Fallback-Upload in dieselbe Prüfung einbeziehen. Nicht pauschal die Bedeutung von `IsComplete({})` ändern: andere Implementierungen verwenden ungültige Tokens auch für Pfade ohne asynchrone Arbeit.

**Fehlender Test.** [Test.GpuAssetCache.cpp](../../tests/unit/graphics/Test.GpuAssetCache.cpp), `ControllableTransferQueue` 36–86, erzeugt gültige Tokens und ignoriert deren Inhalt bei der Completion-Abfrage. Eine gezielte Ablehnung bei operationalem Backend muss initialen Upload, Reload und Fallback abdecken: kein falsches `Ready`, gültiger Vorgänger bleibt verfügbar. Quelle und Peer bestätigen den Defekt; der Vulkan-Fehlerpfad wurde hier nicht ausgeführt.

### SRC-GRAPHICS-002 — Staging-Ring verwechselt freie und belegte Bereiche

**Fundstellen.** [Backends.Vulkan.Staging.cpp](../../src/graphics/vulkan/Backends.Vulkan.Staging.cpp), `Allocate` 73–105, Wrap 79–93, Überlappungsprüfung 95, `Retire` 108–115, `GarbageCollect` 118–126. [Backends.Vulkan.Transfer.cpp](../../src/graphics/vulkan/Backends.Vulkan.Transfer.cpp), Reserve bei 495, Submit/Retire 252–262, Collection 1089–1101. [RHI.TransferQueue.cppm](../../src/graphics/rhi/RHI.TransferQueue.cppm):31–35 erlaubt Upload-Aufrufe von beliebigen Threads.

**Deterministischer voller Ring.** Kapazität 1024, Alignment 256:

1. 256 Bytes reservieren, mit Ticket 1 retiren: `[0,256)`.
2. 512 Bytes reservieren, Ticket 2: `[256,768)`.
3. Ticket 1 collecten: Head 256, Tail 768.
4. 256 Bytes reservieren, Ticket 3: `[768,1024)`.
5. 256 Bytes reservieren, Ticket 4: `[0,256)`; der Ring ist voll, Head und Tail sind beide 256.
6. Weitere 256 Bytes werden trotzdem als `[256,512)` zurückgegeben und überlappen das noch belegte Ticket 2.

**Zweiter reproduzierter Fall.** `Allocate(256) → GarbageCollect(0) → Allocate(256)` liefert denselben Bereich zweimal, wenn noch kein Retire erfolgt ist: Collection setzt bei leerer In-Flight-Liste Head/Tail zurück, obwohl eine Pending-Reservierung existiert. Außerdem können mehrere Upload-Aufrufer gemeinsame Pending-Bereiche unter dem Ticket des ersten Submitters retiren. Einzelne Methodenschlösser schützen diese Transaktion nicht.

**Auswirkung und Nachweisgrenze.** Belegte Upload-Daten können überschrieben oder zu früh wiederverwendet werden. Beide oben beschriebenen Allocator-Fälle wurden mit den extrahierten Originalmethoden in einem CPU-Harness ausgeführt. GPU-Handles waren Platzhalter; dies war weder Vulkan-Ausführung noch eine beobachtete Produktionsrace. Ob heutige Aufrufer tatsächlich gleichzeitig hochladen, wurde nicht vollständig nachverfolgt.

**Kleinste Korrektur.** Den belegten Wrap-/Vollzustand eindeutig darstellen und Überschneidungen mit jedem lebenden Bereich verhindern. Pending-Reservierungen beim Collect nicht zurücksetzen. Reserve, Copy, Submit/Retire und Collection müssen zusammen einen korrekten Lebenszyklus haben, etwa mit konsistenter Serialisierung oder individueller Reservierungszuordnung. Ein bloßes `<` → `<=` korrigiert die Unterscheidung zwischen leer und voll nicht. Vorhandene Belt-Regressionstests wurden nicht gefunden; die zwei Fälle gehören in einen Test gegen die echte Implementierung.

### SRC-GRAPHICS-003 — Einzeltextur-Upload prüft Copy-Größe nicht

**Auslöser.** `UploadTexture` erhält für eine 8×8-RGBA8-Textur 64 Bytes, obwohl der vollständige Copy 256 Bytes liest. Der Pfad reserviert und kopiert `dataSize`, verwendet im GPU-Copy jedoch die volle Mip-Ausdehnung. Der GPU-Read greift dadurch über die reservierte Upload-Region hinaus auf fremde oder nicht initialisierte Staging-Daten zu; ein CPU-Buffer-Overflow wird hier nicht behauptet.

**Fundstellen.** [Backends.Vulkan.Transfer.cpp](../../src/graphics/vulkan/Backends.Vulkan.Transfer.cpp):584–590 reserviert mit festem Alignment 4 und kopiert `dataSize`; 612–619 beschreibt den vollständigen Bildbereich. Der Full-Chain-Pfad nutzt bereits Größen-/Layoutregeln aus [RHI.TextureUpload](../../src/graphics/rhi/RHI.TextureUpload.cppm).

**Kleinste Korrektur.** Vor Reservierung Format, Subresource-Größe und Eingabemenge mit den vorhandenen `ComputeSubresourceUploadSize`- und `RequiredBufferOffsetAlignment`-Regeln prüfen. Die Policy des Full-Chain-Pfads wiederverwenden, statt einen zweiten Rechner einzuführen.

**Testlücke.** [Test.GpuAssetCache.cpp](../../tests/unit/graphics/Test.GpuAssetCache.cpp):126 definiert eine 8×8-RGBA8-Textur; der Test um 209 verwendet dafür nur 64 Bytes. Der permissive Mock verdeckt den Größenunterschied. Ungültige Größen müssen vor GPU-Recording abgelehnt werden; gültige Format-/Alignment-Fälle ebenfalls sichern. Der reale Vulkan-Copy wurde in dieser Sitzung nicht ausgeführt.

### SRC-GRAPHICS-004 — Deferred Delete verwendet nach EndFrame den falschen Slot

**Auslöser.** Zulässige Backend-Sequenz: `BeginFrame(0) → Ressource verwenden → EndFrame(0) → Present(0) → DestroyBuffer → BeginFrame(1)`. Nach `EndFrame` zeigt `m_FrameSlot` bereits auf Slot 1. `DeferDelete` hängt die native Löschung dort an. Beim Beginn von Slot 1 wird auf dessen alten bzw. initialen Fence gewartet und dessen Löschliste geleert, obwohl die Verwendung in Slot 0 noch laufen kann.

**Fundstellen.** [Backends.Vulkan.Device.cpp](../../src/graphics/vulkan/Backends.Vulkan.Device.cpp), Slot-Wechsel 3111/3149, Flush beim Frame-Beginn 2723, `DestroyBuffer` 4356–4384, `DeferDelete` 5721–5733. Weitere native Ressourcentypen verwenden denselben Löschmechanismus. CPU-Handle-Invalidierung und sichere native Freigabe sind dabei unterschiedliche Fragen.

**Kleinste Korrektur.** Löschung an den tatsächlich betroffenen Recording-/Submission-Abschluss binden. Der nächste Aufnahme-Slot allein liefert diesen Nachweis nicht. Den Vertrag für Freigaben innerhalb und zwischen Frames explizit testen, bevor der Mechanismus geändert wird.

**Reichweite.** Dies ist ein statisch belegter generischer Backend-Lifecycle-Fehler. Ein konkreter aktueller Endnutzerablauf, der genau in diesem Fenster freigibt, wurde nicht nachgewiesen; höhere Caches können zusätzliche Retirement-Abstände einhalten. [Test.RHIResourceSlotRecycling.cpp](../../tests/contract/graphics/Test.RHIResourceSlotRecycling.cpp) prüft Handle-/Slot-Verhalten mit Null-Backend, nicht native Vulkan-Fences. Es gab keinen GPU-Repro.

### SRC-GRAPHICS-005 — Fehlgeschlagene Transient-Allokation lässt Frame weiterlaufen

**Auslöser und Folge.** Eine Transient-Texture oder ein Transient-Buffer kann nicht angelegt werden. Der Renderer meldet den Fehler und ruft den Diagnose-Hook auf, kehrt aber nicht aus der Frame-Ausführung zurück. Nachfolgend können Barrieren und Attachment-Recording mit unvollständigen Ressourcen beginnen; der Ausführungspfad kann schließlich `Succeeded = true` setzen.

**Fundstellen.** [Graphics.Renderer.cpp](../../src/graphics/renderer/Graphics.Renderer.cpp), Allokationsfehler 3118–3126, Single-Queue-Ausführung 3610–3636, Erfolg 4111, konkrete CreateTexture-/CreateBuffer-Fehler 7710–7713 und 7744–7747. [RHI.Device.cppm](../../src/graphics/rhi/RHI.Device.cppm):200 definiert den Validierungs-Hook standardmäßig ohne Abbruchwirkung. Mock zeichnet Diagnose auf; Vulkan markiert den Zustand als nicht operational. Das ersetzt nicht den fehlenden Abbruch vor dem generischen Recording.

**Kleinste Korrektur.** Bei fehlenden Ressourcen bzw. gescheiterter Recipe-Graph-Validierung vor dem Command-Recording mit negativem Ergebnis zurückkehren und die Diagnose behalten.

**Fehlender Test.** In [Test.RendererFrameLifecycle.cpp](../../tests/contract/graphics/Test.RendererFrameLifecycle.cpp) nach `PrepareFrame` den nächsten passenden Buffer-/Texture-Create gezielt scheitern lassen: keine Pass-Ausführung/Recording, kein erfolgreicher Execute-Status. Der genaue Zeitpunkt muss den Transient-Pfad treffen. Diesen Test haben wir nicht hinzugefügt oder ausgeführt; der Befund beruht auf dem verbundenen Quellfluss.

### SRC-GRAPHICS-006 — Contribution-Vertrag ist breiter als sein Build

**Auslöser.** Ein Pass-Autor liefert eine formal gültige aktive Contribution mit einem nicht implementierten Kind oder abweichendem Anchor/Queue-Vertrag. Describe/Validate akzeptiert die Metadaten. Build unterstützt jedoch nur bestimmte eingebaute Passes und deren fest verdrahtete Positionen/Ressourcen; es kann erfolgreich zurückkehren, ohne die beschriebene Contribution entsprechend zu bauen.

**Fundstellen.** [Graphics.FrameRecipe.cpp](../../src/graphics/renderer/Graphics.FrameRecipe.cpp), Queue-Introspektion 996, Validator 1133–1244, Anchors 1222/1234, Build-Describe 1487–1499, VisualizationOverlay 2190–2206, SelectionOutline/DebugView 2420–2456, ImGui 2480–2499, Ergebnis 2502–2520. Der DebugView-Input ist eine teilweise Ausnahme vom fest verdrahteten Ressourcenbild. `ValidateRecipeCompiledGraph` 1292–1367 prüft Schreibrechte importierter Ressourcen, nicht die Gleichheit von beschriebenen und gebauten Passes.

**Reichweite.** [Test.FrameRecipeContract.cpp](../../tests/contract/graphics/Test.FrameRecipeContract.cpp):886 beschreibt etwa eine eigene ID 9000 mit Kind Culling und AsyncCompute, prüft aber die Introspektion, nicht den Build dieser Contribution. Der Renderer registriert bei [Graphics.Renderer.cpp](../../src/graphics/renderer/Graphics.Renderer.cpp):2984–2988 die Defaults. Deshalb ist dies eine Erweiterungs-API-Lücke, kein nachgewiesener Laufzeitfehler der heutigen Default-Recipe.

**Kleinste Korrektur.** Nicht unterstützte aktive Kinds und abweichende Anchor-/Queue-/Ressourcenformen beim Build eindeutig ablehnen. Nur einen tatsächlich benötigten Erweiterungsfall zusätzlich implementieren, falls das der Produktbedarf ist. Ein allgemeines Plugin-Framework ist durch diesen Befund nicht begründet. Eine Umdeutung der Metadaten zu reiner Introspektion wäre eine bewusste Vertragsentscheidung, kein stiller Fix.

**Fehlender Test.** Custom-Contributions an den tatsächlichen Build übergeben und für jedes akzeptierte Feld nachweisen, dass es angewendet wird; sonst muss der Build scheitern. Die 54 vorhandenen `FrameRecipeContract`-Tests wurden separat erfolgreich ausgeführt. Das schließt diese nicht abgedeckte Lücke nicht.

### SRC-GRAPHICS-007 — Große Property-Smoothing-Dispatches überschreiten die X-Grenze

**Auslöser.** Der Workspace akzeptiert bis `1 << 24` Rows und `1 << 26` Edges. Resident Load/Store sowie Normal-/Bilateral-Pfade berechnen eindimensionale Dispatches mit `(threads + 63) / 64`. Bereits 4.194.241 Elemente ergeben 65.536 Gruppen. Das ist ungültig auf Geräten mit `maxComputeWorkGroupCount[0] = 65.535`, einer zulässigen Vulkan-Grenze.

**Fundstellen.** [Graphics.PropertyFilter.cpp](../../src/graphics/renderer/Graphics.PropertyFilter.cpp):173, 228, 287; [Runtime.MeshFieldOperations.Smoothing.cpp](../../src/runtime/Editor/Operations/Runtime.MeshFieldOperations.Smoothing.cpp):726 und 1020–1021 führen in diese Pfade. [Backends.Vulkan.CommandContext.cpp](../../src/graphics/vulkan/Backends.Vulkan.CommandContext.cpp):514–518 reicht den Dispatch durch. Die [Vulkan-Dispatch-Regel](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDispatch.html) verlangt die Einhaltung des tatsächlichen Gerätelimits; die [Limits-Spezifikation](https://docs.vulkan.org/spec/latest/chapters/limits.html) nennt die garantierten Mindestgrenzen.

**Kleinste Korrektur.** Das Gerätelimit konsistent vorab berücksichtigen. Wenn die bisher akzeptierte Kapazität erhalten bleiben soll, Dispatches aufteilen; andere Point-Kernels zeigen bereits Paging-Muster. Eine spätere Ablehnung nach erfolgreicher großer Workspace-Annahme sollte keine falsche Erfolgsmeldung hinterlassen.

**Nachweis und Testlücke.** Eine C++-Grenzprobe bestätigt die 65.536 Gruppen, nicht die GPU-Ausführung. [Test.PropertySmoothingWorkspaces.cpp](../../tests/contract/graphics/Test.PropertySmoothingWorkspaces.cpp), `PropertyFilterWorkspace` um 62 und 136, verwendet kleine Graphen. Ein Grenztest muss den echten Dispatch-Aufbau bei 65.535 und 65.536 Gruppen prüfen und sicherstellen, dass kein einzelner Dispatch das simulierte Gerätelimit verletzt.

### SRC-GRAPHICS-008 — Konstante große Scalar-Werte verlieren ihre Visualisierung

**Auslöser.** Bei einem konstanten Scalar-Wert wird eine leere Range durch `min -= 0.5f; max += 0.5f` erweitert. Bei `16777216.0f` runden beide Ergebnisse auf denselben Float zurück. Der nachfolgende Packet-Pfad verwirft die weiterhin leere Range; der beabsichtigte Visualisierungspuffer wird nicht erzeugt.

**Fundstellen.** [Runtime.VisualizationRecipes.cpp](../../src/runtime/Visualization/Runtime.VisualizationRecipes.cpp), Range 298–308, `AppendScalarPacket` um 340. Aufrufer: [Runtime.RenderExtraction.Recipes.cpp](../../src/runtime/Rendering/Runtime.RenderExtraction.Recipes.cpp):199; um 188–193 existiert bereits eine relativ zur Größenordnung arbeitende Expansion in einem anderen Fall. [Graphics.VisualizationSyncSystem.cpp](../../src/graphics/renderer/Graphics.VisualizationSyncSystem.cpp):422–423 wiederholt die problematische absolute Expansion im Fallback.

**Kleinste Korrektur.** Eine gemeinsame, größenabhängige und finite Range-Regel verwenden und beide Stellen daran ausrichten. Den Extremfall nahe dem größten endlichen Float ausdrücklich behandeln. Den gemeinsamen Owner nach den erlaubten Schichtabhängigkeiten wählen; keine neue graphics→runtime-Abhängigkeit einführen.

**Nachweis und Testlücke.** Die C++-Probe bestätigt den Kollaps bei `16777216.0f`. [Test.VisualizationRecipes.cpp](../../tests/contract/runtime/Test.VisualizationRecipes.cpp):648–663 deckt lediglich einen kleinen konstanten Wert `2.0f` ab. Große positive und negative konstante Werte sowie finite Randwerte ergänzen. Keine vollständige UI-/GPU-Reproduktion durchgeführt. Für die Zusammenführung mit dem Runtime-Audit ist dies **ein** bereichsübergreifender Defekt, keine Aufforderung zu doppelten Tasks; der historische BUG-059 ersetzt den aktuellen Nachweis nicht.

## Bereits vorhandene Aufgaben und offene Nachweise

Diese Punkte wurden im Audit wiedergefunden und haben bereits einen Besitzer. Nicht unter neuen Berichtsschlüsseln erneut als unentdeckte Bugs anlegen.

| Bestehende Aufgabe | Bezug und offene Arbeit |
| --- | --- |
| [GRAPHICS-157 — Bindless retirement](../../tasks/backlog/rendering/GRAPHICS-157-bindless-slot-completion-retirement.md) | `VulkanBindlessHeap::Update/Free/Flush` in [Bindless.cpp](../../src/graphics/vulkan/Backends.Vulkan.Bindless.cpp):164/180/203: Descriptor-Slots erst nach Abschluss aller Leser wiederverwenden; Lebensdauer der zugehörigen Textur mitbehandeln. Abnahme-/GPU-Nachweise bleiben Sache dieses Tasks. |
| [BUG-232 — verlorener Commit-Copy](../../tasks/backlog/bugs/BUG-232-gpuworld-commit-copy-lost-on-failed-submit.md) | [GpuWorld.cpp](../../src/graphics/renderer/Graphics.GpuWorld.cpp):1124–1128 quittiert den aufgenommenen Commit und setzt `PositionShadowStale` zurück, bevor erfolgreicher Submit feststeht. Ein später fehlgeschlagener Submit kann den Copy verlieren. Nicht mit 004 gleichsetzen: anderer Completion-Vertrag. |
| [GRAPHICS-105 — Shading und Attribute-Authority](../../tasks/active/GRAPHICS-105-unified-mesh-shading-and-attribute-source-authority.md) | Maßgeblich für Attributquelle und gemeinsame Darstellung; historischer Text ist nicht automatisch ein noch vorhandener Fehler. |
| [GRAPHICS-153 — GPU reuse/minimal IO](../../tasks/active/GRAPHICS-153-gpu-buffer-reuse-and-minimal-io.md) und [RUNTIME-294 — Property residency](../../tasks/active/RUNTIME-294-migrate-gpu-methods-to-property-residency.md) | Reale Verantwortungsgrenze zwischen GPU-Speicherbesitz und Methoden-/ECS-Veröffentlichung. Vor neuen Residency-Tasks aktuellen Implementierungsstand abgleichen. 007 wird dadurch nicht automatisch abgedeckt. |
| [GRAPHICS-164 — Pipeline-Initialisierung](../../tasks/backlog/rendering/GRAPHICS-164-pipeline-init-block-helpers.md) | Bereits erfasste Bereinigung ähnlicher Init-Blöcke. Fehlerpfade, Veröffentlichungszeitpunkt und Erzeugungsreihenfolge erhalten; die Schätzung des Tasks ist keine in diesem Audit gemessene Einsparung. |

### Verworfene Fehlerhypothese: Capture beim Minimieren

Ein framebasiertes Capture-Budget läuft beim minimierten Fenster nicht weiter, weil dann keine Frames gerendert werden. Das allein ist **kein nachgewiesener Agent-Hänger**. [Runtime.AgentOperations.Capture.cpp](../../src/runtime/Agent/Runtime.AgentOperations.Capture.cpp):128–137 bricht Agent-Aufnahmen ab, wenn der Viewport nicht präsentierbar ist. Der UI-Pfad kann Pending bleiben und nach Wiederherstellung fertig werden; Rebuild und Shutdown brechen explizit ab.

Anker: [Runtime.Engine.cpp](../../src/runtime/Kernel/Runtime.Engine.cpp):716; [Runtime.ViewCapture.cpp](../../src/runtime/Rendering/Runtime.ViewCapture.cpp):537–538; [Graphics.Renderer.cpp](../../src/graphics/renderer/Graphics.Renderer.cpp):1599/1649, 7824–7829 und 7852. Lediglich die Behauptung im Kommentar, das Frame-Budget begrenze auch beim Minimieren die Wartezeit, ist ungenau. Dieses Thema nicht als weiteren P1/P2-Laufzeitbug übernehmen. Die unabhängige Gegenprüfung hat die ursprüngliche Hang-Vermutung ausdrücklich eingeschränkt.

## Aufräumen, Organisation und durchgehender Stil

### Hauptschichten behalten

Die bestehende Aufteilung `rhi / assets / vulkan / framegraph / renderer` trennt echte Zuständigkeiten. `IDevice` mit Null-/Vulkan-Backends, Rendergraph-Compiler und Barrieren, Snapshot-Grenzen sowie Generationen, Leases und verzögertes Retirement tragen überprüfbare Verträge. Sie sind keine pauschal zu entfernende Abstraktion. Der strenge Layering-Check bestand ohne Allowlist-Einträge; er belegt Abhängigkeitsrichtungen, nicht Fehlerfreiheit oder vollständige Stileinheit.

| Bereich | Gezählt: C++-Zeilen | Empfehlung |
| --- | ---: | --- |
| renderer | 50.525 | Compute-Familien besser auffindbar machen und übergroße Owner verkleinern |
| vulkan | 12.096 | Zuerst Lebensdauer/Transfer reparieren, danach ähnliche Bootstrap-/Resource-Pfade prüfen |
| framegraph | 5.684 | Compiler und Validierung als eigene Verantwortung behalten |
| rhi | 5.154 | Typisierte Verträge und gemeinsam nutzbare Upload-Regeln behalten |
| assets | 992 | Kleine Upload-/Lease-Zustandsmaschine behalten, Fehlerübergänge korrigieren |

Die Zahlen sind eine Strukturaufnahme, keine Komplexitätsmetrik. Die großen Einzeldateien sind [Renderer](../../src/graphics/renderer/Graphics.Renderer.cpp) mit 11.214 Zeilen, [Vulkan Device](../../src/graphics/vulkan/Backends.Vulkan.Device.cpp) mit 5.750, [RenderGraph Compiler](../../src/graphics/framegraph/Graphics.RenderGraph.Compiler.cpp) mit 3.118, FrameRecipe mit 2.720 und GpuWorld mit 2.611.

### Kleine Schritte mit tatsächlichem Nutzen

1. **Fehler vor Verschiebungen.** 001–005 verändern Verhalten und brauchen gezielte Regressionen. Eine parallele mechanische Umorganisation würde den Review erschweren und ihre Ursache nicht beseitigen.
2. **Compute-Dateien innerhalb renderer gruppieren.** `Point*`, `PropertyFilter`, FPS, CPD und SparseCG bilden einen plausiblen `renderer/compute/`-Bereich. Nur Dateipfade und Build-Referenzen ändern; bestehende Modulnamen und Target-Grenzen zunächst behalten. GPU-Ressourcen bleiben Graphics, CPU-Algorithmen Geometry, ECS-/Methodenveröffentlichung Runtime. Das ist eine Organisationsempfehlung, kein offener Funktionsbug.
3. **Eine vorhandene Pass-Familie aus Renderer herauslösen.** Factory-Region 4546–5668 und Init-Region 5790–7006 sind konkrete Kandidaten. In den vorhandenen fachlichen Owner verschieben; kein neuer Registry-/Facade-Layer nur zur Verringerung der Dateilänge.
4. **Vulkan-Initialisierung erst nach Fehlerbehebung konsolidieren.** `CreateSwapchainResources` um 1197 und der Bootstrap-Bereich 1959–2114 in Device sind Vergleichsstellen. Diagnose und partielle Aufräumzustände vor Zusammenlegung vergleichen. Keine pauschale Gleichheit oder gemessene Einsparung behauptet.
5. **Bestehenden GRAPHICS-164 nutzen.** Wiederholte Pipeline-Init-Muster mit den bestehenden Hilfen zusammenführen. Die Unterschiede der Fehler- und Publish-Pfade nicht wegabstrahieren.

### Stil nach überprüfbaren Achsen

| Achse | Befund | Konsequenz |
| --- | --- | --- |
| Module und Dateinamen | Überwiegend durchgehende C++-Modulnamen, getrennte Interfaces/Implementierungen und typisierte Handles | Konvention behalten; allein für neue Ordner keine Module umbenennen |
| Fehlerbehandlung | `bool`, Statuswerte, Operational-Zustand und Diagnose-Hooks sind je Schicht vorhanden, werden aber nicht überall bis zur Abbruchentscheidung getragen; 001 und 005 sind konkrete Folgen | Fehlerübergänge an Ownership-Grenzen vereinheitlichen; kein pauschales neues Result-Framework |
| Lebensdauer | Generationen, Leases und Retirement sind sinnvoll, aber Submission-Abschluss, Frame-Slot und „Upload angenommen“ werden nicht überall sauber getrennt; 001, 002, 004, GRAPHICS-157, BUG-232 | Completion-Vertrag präzisieren und gegen Ablehnung/fehlgeschlagenen Submit prüfen |
| Tests | Viele nützliche CPU-Vertragstests; permissive Transfer-Mocks und Null-Backend beweisen keine native GPU-Lebensdauer | Fehler injizierbar machen; GPU-Nachweis getrennt benennen |
| Lokale Schreibweise | Manche Compute-Pfade nutzen sehr dichte einzeilige Bedingungen, Material-/Visualisierungscode ist konventioneller gegliedert | Bei fachlicher Änderung an umgebenden Stil angleichen; keine flächige Formatierungs-PR |
| Dokumentation | Teilweise stärkere oder ältere Zusagen als Implementierung | Betroffene Zusage zusammen mit dem Vertrag reparieren |

Konkrete Dokumentationsanker: [GpuAssetCache.cppm](../../src/graphics/assets/Graphics.GpuAssetCache.cppm):49–53 beschreibt `RequestUpload` ohne `IDevice`-Aufrufe, obwohl der Manager Ressourcen über das Device anlegt. Daraus folgt allein noch kein nachgewiesener Datenrace. Alte Scaffold-Kommentare in FrameRecipe 2181–2189 und Renderer 10306–10323 sind kein Beleg fehlender Pipelines. Die Renderer-README ist mit 2.753 Zeilen ebenfalls schwer navigierbar; thematische Kürzung/Verlinkung erst anhand aktuell gültiger Verträge vornehmen.

Das Null-Backend unter `renderer/Backends/Null` und Vulkan als direktes Unterverzeichnis sind asymmetrisch, aber kein dringender Umbaugrund. Für `TextureManager::Reupload` wurden keine aktuellen Aufrufer in `src`/`tests` gefunden; Dokumentation beschreibt einen Streaming-Pfad. Vor Entfernung öffentliche Nutzung und beabsichtigten Vertrag klären. Weder vollständige Streaming-Funktion noch sichere Löschbarkeit wurde nachgewiesen.

## Tatsächlich ausgeführte Verifikation

Die folgenden Befehle wurden in dieser Sitzung vor Erstellung des Berichts ausgeführt. Die Konfiguration war Debug/headless, mit Clang 23 und deaktivierten Sanitizern. Der Headless-Build enthält keinen Nachweis des Vulkan-Backends.

```bash
# Im Repository; Ergebnis: bestanden, 960 Dateien,
# 10.266 Imports/Includes, 89 CMake-Links, 0 Allowlist-Einträge/Verstöße.
python3 tools/repo/check_layering.py --root src --strict

cmake --build /home/alex/Documents/IntrinsicEngine/build/ci \
  --target IntrinsicGraphicsAssetsUnitTests IntrinsicGraphicsContractCpuTests -j 4

# Ergebnis: 210 Tests bestanden, 0 fehlgeschlagen.
ctest --test-dir /home/alex/Documents/IntrinsicEngine/build/ci \
  --output-on-failure --timeout 60 --no-tests=error \
  -R '^(GpuAssetCache|GpuWorldPositionPreviewContract|GpuWorldPositionCommitContract|GpuWorldLifetimeContract|RHIResourceSlotRecycling|RHIPlacedMemoryContract|RendererFrameLifecycle|RenderGraphValidation|RenderRecipeConfig|RenderCommandRouter|RenderSubsystemRegistry|VisualizationPropertyBufferResidency|SharedRenderRecipeExecution|RenderingContract)\.'

# Ergebnis: weitere 54 Tests bestanden, 0 fehlgeschlagen.
ctest --test-dir /home/alex/Documents/IntrinsicEngine/build/ci \
  --output-on-failure --timeout 60 --no-tests=error \
  -R '^FrameRecipeContract\.'
```

Zusätzlich wurden die temporären C++-Proben `staging-ring-repro.cpp` und `graphics-boundary-probe.cpp` kompiliert und ausgeführt. Die erste verwendet unveränderte extrahierte `Allocate`-/`Retire`-/`GarbageCollect`-Methoden mit Stand-ins für Vulkan-Handles; ihre Assertions bestätigen die derzeitige Fehlererscheinung, nicht das gewünschte Sollverhalten. Die zweite prüft den Float-Kollaps und die Dispatch-Arithmetik. Das sind keine eingecheckten Regressionstests gegen die fertig reparierte Implementierung. Sie brauchen weder Leistungsinterpretation noch Experiment-Abnahme, da sie deterministische bekannte Antworten prüfen.

**Aussagegrenze:** 264 grüne Bestandstests belegen den getesteten CPU-Bestand. Sie beweisen weder Vulkan-Korrektheit noch die Abdeckung der acht neuen Fälle. Keine reparierte Version und kein Mutationstest eines Fixes liegen vor. Für eine spätere Reparatur zuerst eine gegen den aktuellen Fehler fehlschlagende Soll-Assertion an der echten Implementierung nachweisen.

### Zu Beginn beobachteter CI-Stand

Der Default-Branch war nicht vollständig grün. Diese bereits vorhandenen CI-Probleme sind keine aus diesem Audit abgeleiteten Graphics-Regressionsbelege:

- [CPU-CI 38869189381](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38869189381): Build erfolgreich; Testselektion bricht vor der vollständigen CPU-Ausführung ab, weil `RenderWorldPoolEngineWiring.SourceRules` keinem registrierten Producer zugeordnet wird. Das anschließend fehlende Timing-Artefakt ist ein Folgefehler. Inzwischen unter [BUG-244](../../tasks/backlog/bugs/BUG-244-cpu-selection-rejects-source-rules-producer.md) erfasst.
- [Docs-CI 38869189384](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38869189384): erfolgreich.
- [Nightly 38854737500](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38854737500), älterer Quellstand: Benchmark-Seal lehnt Backend-Namen für `geometry.harmonic_field.smoke.json` und `geometry.property_smoothing.variational_fit_solvers.smoke.json` ab (`cpu_reference_sparse_cholesky`, `cpu_admm_sparse_cholesky`). Kein Nachweis einer numerischen Graphics-Regression. Inzwischen [BUG-245](../../tasks/backlog/bugs/BUG-245-nightly-smoke-backend-manifest-mismatch.md).

Diese Statusangaben sind eine Beobachtung vom Auditbeginn, kein Versprechen über den späteren aktuellen Branchstatus. Die BUG-244/245-Dateien wurden von der parallelen Assets-Arbeit angelegt, nicht von diesem Audit.

## Unabhängiger Review und lokale Belege

Der Peer erhielt die Originalfragen und den Quellstand. Die erste Runde konnte aufgrund fehlender Dateizugriffe die Quelle nicht lesen und war ausdrücklich **kein** Quell-Review. Nach Korrektur des Lesezugriffs prüfte dieselbe unabhängige Peer-Sitzung die vier Themen anhand der Quelle. Read-only und keine weitere Delegation waren durch `Read/Grep/Glob`-Tool-Allowlist und strikte MCP-Konfiguration technisch begrenzt.

| Nachweis | Wert |
| --- | --- |
| Richtung / Modell | Codex → Claude; angefordert und zurückgemeldet `claude-opus-5-5` |
| Effort | `max` angefordert; im Review nicht unabhängig sichtbar |
| Sitzung | `f0a5551c-eaca-4bf2-8d7c-a5ec692e1213` |
| Ausgangspaket SHA-256 | `301623c82ecd4a1273b6b028b40a610932f0dc51a79627730bfb9612529f7deb` |
| Austauschpaket SHA-256 | `28d07c67fefef2955f83674c799e2b09ee40bfd7237b7f9825765fc3e6bd7e7e` |
| Bezugsobjekt | Quellstand `66da33fd6bfabb20408607b8361354fc9adb420d`; kein Fix-Diff |

Das tatsächliche abschließende Urteil, auf den geprüften Umfang begrenzt:

> **Verdict: approve.** Das gilt für A und B als Defekte (Erreichbarkeit des Race nur mittel), für C als Einschränkung mit falschem Kommentar und für D als API-Lücke. **Revise required** gilt, falls die Claims darüber hinausgehen: Hardware-Reproduktion, C als Hänger oder D als Laufzeitbug.

A entspricht 002, B entspricht 001, C der verworfenen Capture-Hang-Hypothese, D entspricht 006. Das ist keine Freigabe aller acht Befunde oder der Organisationsempfehlungen. Die Peer-Antwort bezeichnet den zusätzlichen FrameRecipe-Testlauf noch als ausstehend; der Hauptagent führte die oben aufgeführten 54 Tests anschließend separat erfolgreich aus. Die Originalantwort bleibt unverändert erhalten.

Zusätzliche lokale Belege liegen im ignorierten Verzeichnis `tasks/evidence/SRC-GRAPHICS-AUDIT-20261016/`: `graphics-audit-checks.json`, drei Build-/Testlogs, beide Probe-Quelldateien, Peer-Pakete, Antworten und `graphics-peer-record.json`. Dieses Verzeichnis ist temporäre lokale Evidenz und wird nicht automatisch mit Git übertragen. Der Bericht enthält deshalb die entscheidenden Abläufe, Resultate und Einschränkungen selbst; das Fehlen dieses Verzeichnisses in einem anderen Checkout macht daraus keinen ausgeführten GPU-Nachweis.

## Übergabe an Codex 6 Astra Ultra

1. Über [src-audits.md](../reviews/src-audits.md) alle Bereichsberichte sammeln und jeweils Quellstand/Belegstatus erhalten. Aktuellen CI-Stand erneut prüfen; bestehende BUG-244/245 nicht duplizieren.
2. Neue Befunde nach verletztem Vertrag deduplizieren. 008 mit Runtime abstimmen; 007 betrifft den Graphics-Dispatch trotz Runtime-Aufrufer. 001 und Assets-Ladezustände sind nur bei identischer Ursache zusammenzuführen. 004, GRAPHICS-157 und BUG-232 teilen das Thema Lebensdauer, sind aber verschiedene Mechanismen.
3. Die acht Kennungen am aktuellen HEAD erneut prüfen. Zunächst 001/002/003 als zusammenhängende Upload-Sicherheit und danach 004/005 als Frame-/Ressourcenlebensdauer bearbeiten; die Reihenfolge ist eine Empfehlung, keine neu eröffnete Aufgabenliste.
4. Vor jeder Reparatur die angegebenen Fehlerfälle als echte Regressionen nachweisen. Headless-Mocks und CPU-Proben gezielt nutzen; erforderliche Vulkan-/GPU-Validierung separat planen und als solche ausweisen.
5. Bestehende Task-Besitzer weiterverwenden. Neue Tasks nur für danach bestätigte, noch unbesetzte Ursachen anlegen und auf die stabile Berichtsschlüsselstelle verweisen. Dieser Bericht erzeugt keine zusätzlichen aktiven Aufgaben oder HINTS-Einträge.
6. Verhaltensreparaturen getrennt von Dateiverschiebungen und Stilbereinigung halten. Keine neue allgemeine Pass-, Service- oder Lifecycle-Abstraktion allein aus Dateigröße ableiten.

Nicht geprüft wurden reale GPU-Ausführung, Sanitizer, gesamte CPU-Suite und Lastverhalten. Das verbleibende Risiko liegt besonders in nativer Synchronisation und tatsächlicher Aufrufer-Erreichbarkeit; die statischen Befunde dürfen nicht als beobachtete Hardware-Ausfälle ausgegeben werden.
