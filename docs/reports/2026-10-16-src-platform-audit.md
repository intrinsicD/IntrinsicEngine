# Audit von src platform

## Auftrag und Fragen

1. „sollten wir platform aufräumen?“
2. „anders organisieren?“
3. „hat die implementierung einen durchgenden stil?“
4. „gibt es offene punkte, bugs, implementierungs gaps?“

Der Auftrag lautete außerdem: „bitte schaue dir src/platform genau an. nutze das ponytail plugin dafür.“ und „lasse subagenten aus verschiedenen "use-case rollen" und perspektiven darauf schauen.“
Dieser Bericht ist die angeforderte dauerhafte Übergabe für einen Folgeagenten, insbesondere Codex 6 Astra Ultra, und für den späteren Abgleich mit Audits der anderen src-Unterordner.

Gemeinsamer Einstieg für die Gesamtauswertung: [Berichte zu src](../reviews/src-audits.md).

## Ergebnis und Geltungsbereich

**Gezielt aufräumen, die bestehende Organisation beibehalten.** Zuerst die Socket-Fehler, verlorene Eingabeflanken und die Ereignis-Lebensdauer beheben. Danach kleine API- und Dokumentationsreste entfernen. Ein neues Framework, eine gemeinsame Backend-Basisklasse oder neue Unterordner sind dafür nicht erforderlich.

| Merkmal | Stand |
| --- | --- |
| Datum | 2026-10-16, Europe/Berlin |
| Repository | IntrinsicEngine |
| Geprüfter Commit | 66da33fd6bfabb20408607b8361354fc9adb420d |
| Branch beim Audit | main, identisch mit origin/main |
| Quellzustand | src/platform, betroffene Runtime-Quellen und Tests gegenüber diesem Commit unverändert |
| Berichtstatus | Audit abgeschlossen; die vorgeschlagenen Implementierungsänderungen sind offen |
| Primärbereich | Alle 12 Dateien in src/platform, einschließlich CMake und README |
| Nutzung | Ein interaktives Desktop-Fenster oder eine Engine ohne optionales Editor-UI; deterministische Null-Tests; optional ein aktiver lokaler Agent-Client |
| Zielbezug | Zuverlässigkeit der Framework24-Workflows; expliziter Nutzerauftrag. Siehe [Produktziel](../product/framework24-convergence.md) und [REVIEW-004](../../tasks/backlog/architecture/REVIEW-004-framework24-product-convergence-audit.md). |
| Änderungsgrenze | Dieser Auftrag speichert Befunde und Testrezepte. Er implementiert keine Fixes und eröffnet keine zusätzlichen Feature-Aufträge. |

Die IDs PLAT01 bis PLAT12 gehören **zu diesem Bericht**, nicht zum globalen Task-ID-System. Bei späterer Umsetzung diese IDs referenzieren; nicht allein aufgrund der Zeilennummern arbeiten. Zeilen beziehen sich auf den genannten Commit. Symbolnamen und Aufrufketten dienen der erneuten Zuordnung.

Bereits parallel entstandene Änderungen an tasks/SESSION-BRIEF.md sowie BUG-244 und BUG-245 stammen aus einem anderen Audit und wurden hier nicht bearbeitet.

## Orientierung für Folgeagenten

| Verantwortung | Kanonische Quelle | Wichtige Verbraucher |
| --- | --- | --- |
| Fenstervertrag und Ereignistypen | [Platform.IWindow.cppm](../../src/platform/Platform.IWindow.cppm) | Runtime.Engine, Runtime.ImGuiAdapter, SceneInteractionModule |
| Abgefragter Eingabezustand | [Platform.Input.cppm](../../src/platform/Platform.Input.cppm), [Implementierung](../../src/platform/Platform.Input.cpp) | Runtime.InputActions, EditorUiModule, SceneInteractionModule, Kamerasteuerung |
| Backend-Auswahl | [CMakeLists.txt](../../src/platform/CMakeLists.txt), [CreateWindow](../../src/platform/Platform.CreateWindow.cpp), [WindowConfig](../../src/core/Core.Config.Window.cppm) | Runtime.Engine |
| Headless-Fenster und Testereignisse | [Null-Schnittstelle](../../src/platform/backends/null/Platform.Backend.Null.cppm), [Implementierung](../../src/platform/backends/null/Platform.Backend.Null.cpp) | Platform-Tests, Runtime-Tests, expliziter WindowBackend::Null |
| Native Fenster und GLFW-Callbacks | [GLFW-Schnittstelle](../../src/platform/backends/glfw/Platform.Backend.Glfw.cppm), [Implementierung](../../src/platform/backends/glfw/Platform.Backend.Glfw.cpp) | Runtime.Engine; Runtime reicht den nativen Handle an Graphics weiter |
| Lokaler Socket-Transport | [LocalSocket-Schnittstelle](../../src/platform/Platform.LocalSocket.cppm), [Implementierung](../../src/platform/Platform.LocalSocket.cpp) | Runtime.AgentServer |

**Erhalten:** platform → core; keine Graphics-, ECS-, Runtime- oder ImGui-Abhängigkeit in Platform. Die Null/GLFW-Paarung sind zwei echte Implementierungen. Fensterkoordinaten und Framebufferpixel bleiben getrennt. Runtime besitzt die Verdrahtung; Transport und Agent-Protokoll bleiben getrennt. Ein fehlgeschlagenes GLFW-Fenster bleibt geschlossen und fällt nicht still auf Null zurück.

Der strikte Layering-Check fand keine Verletzung. Die Trennung von exportierter .cppm-Oberfläche und privaten .cpp-Implementierungen ist überwiegend eingehalten.

## Befundregister

Evidenzklassen: **R** = isolierter Fehlerfall gegen unveränderte CI-Module reproduziert; **S** = Quellcode und Verbraucher geprüft; **L** = latentes API-Risiko ohne gefundenen aktuellen Produktionsaufrufer; **N** = nativer Nachweis noch erforderlich. R belegt nicht automatisch den vollständigen GUI- oder Runtime-Ende-zu-Ende-Pfad.

| ID | Priorität | Befund | Besitzer für die Umsetzung | Evidenz | Nutzerfragen |
| --- | --- | --- | --- | --- | --- |
| PLAT01 | P1 | SendAll kann trotz Timeout blockieren | platform; Folge in runtime/Agent | R + S | 4 |
| PLAT02 | P2 | Fehlgeschlagene Besitzprobe entfernt aktiven Socket | platform | R | 4 |
| PLAT03 | P2 | Close löscht eine inzwischen andere Datei | platform | R | 4 |
| PLAT04 | P2 | Vollständiger Tastendruck oder Klick innerhalb eines Polls geht verloren | platform/Input | R + S | 4 |
| PLAT05 | P2 | Ereignisse bleiben ohne EditorUiModule dauerhaft gespeichert | runtime/Kernel und Platform-Vertrag | S | 1, 4 |
| PLAT06 | P2 | Minimierungszustand ist nicht hinreichend durch Framebuffergröße abgebildet | platform/GLFW | S + N | 4 |
| PLAT07 | P2 | Null-Callback verliert nachgelieferte Ereignisse | platform/Null | R + L | 4 |
| PLAT08 | P2 | Owning GLFW-Fensterklasse erlaubt Kopien | platform/GLFW | S + L | 3, 4 |
| PLAT09 | P2 | ImGui meldet nicht implementierte Cursorfähigkeiten | runtime/ImGui | S | 4 |
| PLAT10 | P2 | Nachrichtenlimit wird nach Einreihen vollständiger Zeilen geprüft | runtime/Agent | S | 4 |
| PLAT11 | P3 | Überflüssige und produktiv ungenutzte API-Reste | platform und Runtime-Testdoubles | S | 1, 2, 3 |
| PLAT12 | P3 | Kleine Dokumentations- und Stilabweichungen | platform | Scanner + S | 1, 3 |

**Abgleich mit weiteren Audits:** PLAT05, PLAT09 und PLAT10 haben einen Runtime-Besitzer und können im Runtime-Bericht erneut auftauchen. Nach identischem Symbol und Trigger zusammenführen, nicht doppelt implementieren. PLAT01 hat eine Runtime-Folge, aber die unmittelbare Ursache liegt in Platform. Die Reihenfolge der Bericht-IDs ist keine Freigabe, alle Punkte in einem großen Refactoring zu bearbeiten.

### PLAT01 SendAll hält das Timeout nicht ein

**Ort:** Platform.LocalSocket.cpp:67–71, 100–119, insbesondere 105–111. Die FDs sind blockierend. POLLOUT vor send() garantiert nicht, dass die gesamte danach angebotene Nachricht ohne Blockieren geschrieben werden kann.

**Trigger und Folge:** Ein Peer liest nicht; eine Antwort ist größer als der freie Sendepuffer. SendAll(8 MiB, 20 ms) kehrte im isolierten Test nicht innerhalb einer Sekunde zurück. Ein kleiner Payload bestand. In Produktion sendet [Runtime.AgentServer](../../src/runtime/Agent/Runtime.AgentServer.cpp):326 die Antworten; Shutdown():400–404 wartet anschließend auf den Worker. Große Inline-Screenshots sind ein vorhandener Produzent in [Runtime.AgentOperations.Capture](../../src/runtime/Agent/Runtime.AgentOperations.Capture.cpp):145–157. Der vollständige hängende Sandbox-Shutdown wurde nicht ausgeführt.

**Kleinster Fix:** Den eigentlichen Schreibvorgang nicht blockierend ausführen, etwa auf dem Linux-Pfad mit MSG_DONTWAIT zusätzlich zum vorhandenen SIGPIPE-Schutz. EAGAIN/EWOULDBLOCK über die bestehende Poll-Schleife behandeln. Die aktuell dokumentierte Zeitgrenze gilt pro blockiertem Abschnitt, nicht als Gesamtdauer jedes beliebig langen SendAll. Keinen neuen Thread oder Transport ergänzen.

**Regression:** Nicht lesender Peer, große Nachricht, 20-ms-Timeout, externer Watchdog. Erwartung: Timeout; der Watchdog beendet sicher einen fehlerhaften Testprozess. Die zwei bisherigen LocalSocket-Tests verwenden kleine Nachrichten.

### PLAT02 Listen verwechselt SystemError mit einem verwaisten Socket

**Ort:** Platform.LocalSocket.cpp:151–156; ConnectLocalSocket():195–208.

**Trigger und Folge:** Listener A ist aktiv. Ein zweiter Listener prüft denselben Pfad bei erschöpften Dateideskriptoren. Seine Probe liefert SystemError; Listen löscht trotzdem As Socket-Datei und kann anschließend selbst keinen Socket öffnen. Der erste Server bleibt aktiv, verliert aber seinen erreichbaren Pfad. Im isolierten Kindprozess mit RLIMIT_NOFILE=64 reproduziert.

**Kleinster Fix:** Nur einen ausdrücklich als Closed erkannten verwaisten Endpunkt ersetzen. SystemError muss ohne unlink() zurückgegeben werden; erfolgreiche Probe bleibt AddressInUse. Fehler dürfen nicht zu einer Löschfreigabe umgedeutet werden.

**Regression:** FD-Erschöpfung nur im Kindprozess; zweites Listen muss scheitern und der ursprüngliche Pfad muss bestehen bleiben. Bestehender Test RejectsLiveOwnersAndReplacesStaleFiles deckt nur erfolgreiche Live-Probe und tatsächlich verwaisten Socket ab.

### PLAT03 Close entfernt nicht nur die selbst erzeugte Socket-Datei

**Ort:** Platform.LocalSocket.cpp:138–142; zugesicherter Vertrag Platform.LocalSocket.cppm:67.

**Trigger und Folge:** Listener A bindet einen Pfad. Der Pfad wird entfernt und durch eine normale Datei oder Listener B ersetzt. A.Close() entfernt den aktuellen Pfad ungeprüft. Die Löschung einer zwischenzeitlich angelegten normalen Datei wurde reproduziert; der Nachfolger-Socket folgt aus demselben Pfad.

**Kleinster Fix:** Identität der gebundenen Socket-Datei speichern und vor dem Schließen nur den identischen Eintrag entfernen. Bei Move müssen diese Besitzinformationen mitwandern. Ein bloßes Prüfen auf den Dateityp Socket reicht nicht für den Nachfolger-Fall. Das ist keine Zusage gegen adversariale gleichzeitige Pfadänderungen.

**Regression:** A → Pfad entfernen → Ersatzdatei beziehungsweise B → A.Close(); der Ersatz bleibt erhalten. Nur den Kommentar zu lockern würde die Datenverlustgefahr nicht beseitigen.

### PLAT04 Kurze Eingaben verlieren ihre Press-Flanke

**Ort:** Platform.Input.cpp:20–35, 44–53, 61–67. IsKeyJustPressed und IsMouseButtonJustPressed vergleichen nur Anfangs- und Endzustand.

**Trigger und Folge:** QueueKey(F,true), QueueKey(F,false), dann PollEvents(). Ebenso für die Maustaste. Beide JustPressed-Abfragen ergeben false, obwohl eine Press-Flanke eingegangen ist. Der Kontrollfall mit gehaltener Taste besteht. Betroffen sind [Runtime.InputActions](../../src/runtime/Kernel/Runtime.InputActions.cpp):31, [SceneInteractionModule](../../src/runtime/Scene/Runtime.SceneInteractionModule.cpp):85–88 und [EditorUiModule](../../src/runtime/Editor/Runtime.EditorUiModule.cpp):267. Verlorene GUI-Picks wurden nicht live ausgeführt. ImGui verarbeitet zusätzlich die vollständigen Events und ist nicht mit diesem zustandsbasierten Pfad gleichzusetzen.

**Kleinster Fix:** Press-Flanken seit BeginFrame separat speichern, während der aktuelle Haltezustand weiter auf Releases reagiert. Bei wiederholtem SetKeyState(true) keine künstlichen neuen Flanken erzeugen.

**Regression:** Vollständiger Tap in einem Poll erzeugt genau eine Press-Flanke, obwohl der Endzustand released ist; nächster Frame hat keine Flanke. Dasselbe für Maus. Bestehende Hold-Tests erhalten. Die Semantik „mindestens einmal in diesem Frame gedrückt“ ausdrücklich dokumentieren.

### PLAT05 Ohne EditorUiModule fehlt ein Besitzer für das Leeren der Events

**Ort:** Platform.Backend.Glfw.cpp:225–232; Platform.Backend.Null.cpp:57–70. Beide speichern Events zusätzlich zum synchronen Listener. Einziger produktiver DrainEvents-Aufrufer: [ImGuiAdapter](../../src/runtime/ImGui/Runtime.ImGuiAdapter.cpp):459–462. Der Adapter wird im optionalen EditorUiModule erzeugt.

**Trigger und Folge:** Eine Engine-Komposition ohne EditorUiModule erhält laufend Cursor- oder andere Ereignisse. Der Listener verarbeitet sie, aber die zusätzliche Eventliste wird nie geleert. Der unbegrenzte Speicherbestand folgt aus dem Aufrufpfad; es gab keinen Langzeit-Speichertest. Das normale Ausblenden des Editors per G ist nicht dieser Fall: Der Adapter läuft dabei weiter.

**Kleinster Fix:** Im Frame-Lifecycle nach den optionalen Verbrauchern einen definierten Verbrauch beziehungsweise Discard verbleibender Events garantieren. Kein weiterer EventBus. Kein blindes Leeren am Poll-Beginn oder in minimierten Frames: Dabei könnten für ImGui benötigte Releases und Fokusübergänge verschwinden.

**Regression:** Mehrere vollständige Engine-Frames ohne EditorUiModule mit Ereignissen; kein dauerhafter Restbestand. Zusätzlich UI-Verarbeitung und Minimize/Restore-Ereignisse gegen Verlust prüfen.

### PLAT06 Native Minimierung braucht einen eigenen Nachweis

**Ort:** Platform.Backend.Glfw.cppm:27; SetupCallbacks in Platform.Backend.Glfw.cpp:278–354; [Engine::RunFrame](../../src/runtime/Kernel/Runtime.Engine.cpp):756–766.

**Befund:** IsMinimized prüft nur Width==0 oder Height==0. Im tatsächlich eingebundenen GLFW 3.4 verarbeitet X11 den Iconify-Zustand separat. Die lokale Quelle external/vcpkg/buildtrees/glfw3/src/3.4-2448ff4533.clean/src/x11_window.c:1797–1815 setzt window->x11.iconified und ruft _glfwInputWindowIconify auf; :2240–2242 fragt für die Framebuffergröße die Fenstergröße ab. Es gibt im Engine-Backend keinen Iconify-Callback und keine Abfrage von GLFW_ICONIFIED.

**Folge und Unsicherheit:** Der vorgesehene Idle-Pfad kann bei echter X11-Minimierung ausbleiben. Dass die konkrete Desktop-Umgebung dabei keine Nullgröße liefert, ist noch durch einen nativen Test zu bestätigen. Die source-seitige Unvollständigkeit ist belegt; kein Live-Test wurde ausgeführt. GLFWs Wayland-Implementierung liefert bei _glfwWindowIconifiedWayland ausdrücklich false (wl_window.c:2527–2531); eine reine GLFW_ICONIFIED-Ergänzung ist daher kein allgemeiner Wayland-Fix.

**Kleinster nächster Schritt:** Nativen X11-Minimize/Restore-Regressionstest erstellen; anschließend den tatsächlichen Iconify-Zustand ergänzen, ohne Fenster- und Framebuffergröße zu vermischen. Den bewusst begrenzten Wayland-Vertrag separat dokumentieren. Null QueueResize(0,0) allein belegt dieses Verhalten nicht.

### PLAT07 Null-Callbacks können ihre eigene Pending-Queue verändern

**Ort:** Platform.Backend.Null.cpp:60–69.

**Trigger und Folge:** Ein Listener ruft QueueClose auf, während PollEvents gerade m_PendingEvents iteriert. push_back kann Iteratoren invalidieren; das anschließende clear entfernt die nachgelieferten Events. Im Repro mit einem Ausgangsereignis bleibt ShouldClose auch nach dem nächsten Poll false. Das beweist Eventverlust, keinen ausgeführten Use-after-free. Der aktuelle Runtime-Listener reiht keine solchen Events ein; es ist ein latentes Risiko der öffentlichen Null-Testoberfläche.

**Kleinster Fix:** Den anstehenden Batch vor der Iteration herausnehmen, etwa per swap/exchange. Nachgelieferte Events bleiben für den nächsten Poll liegen; das bisherige abschließende clear auf m_PendingEvents entfällt.

**Regression:** Passiver Erst-Callback reiht genau ein Folgeevent ein; der zweite Poll liefert es. Für einen gezielten Sanitizer-Nachweis der Iteratorinvalidierung braucht es einen passenden Mehrere-Ereignisse-Fall; dieser wurde nicht ausgeführt.

### PLAT08 GLFW-Fenster verbieten Kopien nicht

**Ort:** Platform.Backend.Glfw.cppm:19–23, 59; Destruktor Platform.Backend.Glfw.cpp:120–126.

**Trigger und Folge:** Window b = a kopiert den owning GLFWwindow-Zeiger; beide Destruktoren zerstören denselben Handle. GLFWs UserPointer zeigt weiterhin auf das ursprüngliche Objekt. Wegen des benutzerdefinierten Destruktors kann auch ein std::move-Ausdruck auf den Copy-Konstruktor zurückfallen. Kein entsprechender aktueller In-tree-Aufrufer wurde gefunden.

**Kleinster Fix:** Copy-Konstruktor und Copy-Assignment löschen. Keine neue Move-Implementierung erforderlich. LocalSocket zeigt bereits die gewünschte explizite Besitzpolitik.

**Regression:** Compile-time-Assertions für Nichtkopierbarkeit und Nichtverschiebbarkeit im GLFW-fähigen Testtarget; eine native Anzeige ist dafür nicht nötig.

### PLAT09 ImGui meldet nicht implementierte Cursorfähigkeiten

**Besitzer und Ort:** runtime/ImGui, nicht allein platform. Runtime.ImGuiAdapter.cpp:425–426.

**Trigger und Folge:** HasMouseCursors und HasSetMousePos sind gesetzt, aber es gibt unter src keinen Verbraucher von GetMouseCursor oder WantSetMousePos. IWindow hat weder Cursorform- noch Warp-Funktionen. CursorMode steuert nur Normal/Hidden/Disabled. Damit passen zugesagte Fähigkeiten und Implementierung nicht zusammen. Text-/Resize-Cursor und optionales Navigations-Warp werden nicht bedient.

**Kleinster Fix:** Die beiden unbelegten Flags entfernen. Zusätzliche Cursorfunktionen nur mit einem konkreten Nutzerbedarf implementieren. Die lokale ImGui-Headerdefinition beschreibt die Verpflichtungen dieser beiden Flags.

**Regression:** Adaptertest prüft die tatsächlich unterstützten BackendFlags. Bei späterer Implementierung zusätzlich Wirkung am nativen Cursor testen.

### PLAT10 Das Agent-Nachrichtenlimit wird nach der Zeilenentnahme geprüft

**Besitzer und Ort:** runtime/Agent. Runtime.AgentServer.cpp:332–344; MaxMessageBytes in Runtime.AgentServer.cppm:103.

**Trigger und Folge:** Ein Receive ergänzt einen zuvor zulässigen Teilpuffer um die abschließende Zeile. Diese wird bereits in Inbound eingereiht; erst danach wird die Größe des verbliebenen Puffers geprüft. Eine vollständige Nachricht kann deshalb um einen Teil des letzten 4096-Byte-Chunks über dem Limit liegen. Dies ist kein unbegrenzt großer Einzelrequest.

**Kleinster Fix:** Die Länge jeder vollständigen Zeile vor substr/Queueing gegen MaxMessageBytes prüfen und die Prüfung des Restpuffers beibehalten.

**Regression:** Kleine konfigurierte Grenze; exakt erlaubte Zeile akzeptieren, um ein Byte zu lange vollständige sowie über mehrere Receives verteilte Zeile zurückweisen. Kein neuer Lauf dieses Runtime-Pfads wurde im Audit ausgeführt.

### PLAT11 Kleine tote API entfernen

Repo-weite Suche einschließlich Tests, Dokumentation und Tools wurde mit den tatsächlichen Aufrufern abgeglichen.

| Element | Ort | Befund und kleinste Änderung |
| --- | --- | --- |
| IWindow::OnUpdate | Platform.IWindow.cppm:111; beide Backends | Kein externer Produktionscaller. GLFW delegiert PollEvents → OnUpdate, Null umgekehrt. PollEvents behalten, GLFW-Body dorthin ziehen, Alias entfernen. |
| Input::Initialize und m_WindowHandle | Platform.Input.cppm:38,57; Platform.Input.cpp:9–12 | Der Handle wird nur gespeichert und nie gelesen. Methode, Member und beide Backend-Aufrufe entfernen. |
| GetScrollAccum | Platform.Input.cppm:47 | Kein Aufrufer; internen Akkumulator für GetScrollDelta/Update erhalten, nur ungenutzte Oberfläche entfernen. |
| ConsumeInputActivity und m_InputActivity | IWindow und beide Backends | Nur Tests und Testdoubles konsumieren die API. Bei Entfernung Setter, Getter, Member und deren Tests zusammen aktualisieren; nicht mit echter Ereignisverarbeitung verwechseln. |

Betroffene Testdoubles: [Test.ImGuiAdapter.cpp](../../tests/contract/runtime/Test.ImGuiAdapter.cpp):64,77 und [Test.SceneInteractionModule.cpp](../../tests/contract/runtime/Test.SceneInteractionModule.cpp):518,524. Keine neue Dependency und keine Kompatibilitätswrapper erforderlich; der Repositoryvertrag verpflichtet nicht zur Bewahrung ungenutzter interner API.

Die ähnlichen ApplyEvent-Visitoren haben zwei reale Verbraucher. Sie dürfen bleiben. Eine kleine gemeinsame Funktion wäre begründbar, eine neue Backend-Hierarchie allein zur Verkürzung ist nicht angezeigt.

### PLAT12 Dokumentation und Stil angleichen

Der Dokumentationsscanner meldete drei objektive Fehler: fehlende führende Kurzbeschreibung in Platform.IWindow.cppm, Platform.Backend.Null.cppm und Platform.Backend.Glfw.cppm. Außerdem sieben heuristische Review-Hinweise, die keine sieben bestätigten Defekte sind.

[README](../../src/platform/README.md):3 und :74 beschreiben nur Window/Input, obwohl LocalSocket dazugehört; die Layoutskizze lässt LocalSocket aus. Umfangreiche Lifetime-/Sanitizer-Historie kann zu einer knappen aktuellen Vertragsbeschreibung mit Link auf die bestehende Diagnose verkürzt werden. Zuerst den Beleg und seine dauerhafte Zielstelle prüfen, nicht die einzige Erklärung löschen.

Der Grundstil ist konsistent: C++23-Module, m_-Member, explizite Backends und std::visit. Input verwendet teils andere Klammerpositionen, LocalSocket dichtere Einzeiler. Ein kleiner Stilpass genügt. Ownership- und Fehlersemantik aus PLAT01–PLAT08 haben Vorrang vor Formatierung.

## Bewusste Grenzen und zusätzliche Prüfspuren

- IME-Komposition, native Dateidialoge und Multiwindow sind laut Platform-README keine zugesagten Funktionen. Daraus keinen neuen Featureauftrag ableiten.
- [PLATFORM-004](../../tasks/backlog/platform/PLATFORM-004-alternative-platform-backend-onboarding.md) ist bewusst ein Planungsanker für weitere Plattformen. Windows/macOS-Unterstützung nicht im Zuge dieses Cleanups beginnen.
- [UI-079](../../tasks/backlog/ui/UI-079-imguizmo-hidpi-operational-run.md) enthält den noch offenen nativen HiDPI-Gizmo-Nachweis. CPU-Koordinatentests ersetzen ihn nicht.
- GLFW erzeugt bei Fokusverlust synthetische Releases. Ein allgemeiner GLFW-Bug „Tasten bleiben nach Fokusverlust hängen“ wurde daher nicht bestätigt.
- **Zusätzliche Socket-Prüfspur, nicht reproduziert:** ConnectLocalSocket verwendet blockierendes connect ohne Deadline (Platform.LocalSocket.cpp:195–203), auch für die Besitzprobe in Listen. Ein voller Accept-Backlog kann diesen Weg blockieren. Vor einem Fix mit einem isolierten Backlog-Test belegen; unklar/belegt darf niemals als verwaist gelten.
- **Erst bei Nicht-Unix-Promotion relevant:** Test.LocalSocket.cpp bindet POSIX-Header unbedingt ein. Der Laufzeitstatus Unsupported allein macht diese Testquelle nicht auf Windows kompilierbar. Das ist derzeit keine Zusage einer unterstützten Windows-Testmatrix.

## Verifikation aus dem Audit

Der Audit baute nur das fokussierte Platform-Testtarget im bestehenden ci-Preset. Der lokale Cache verwendete Clang 23 und den passenden clang-scan-deps-23, Debug, Null, INTRINSIC_HEADLESS_NO_GLFW=ON.

    cmake --build --preset ci --target IntrinsicPlatformTests
    ctest --test-dir build/ci --output-on-failure --no-tests=error --timeout 60 -R '^(NullPlatform|LocalSocket|PlatformLayering)\.'
    python3 tools/repo/check_layering.py --root src --strict
    python3 tools/agents/skills/intrinsicengine-source-documentation/scripts/audit_source_documentation.py --root . --path src/platform

| Check | Tatsächliches Ergebnis |
| --- | --- |
| Build IntrinsicPlatformTests | Erfolg |
| LocalSocket.RoundTripsDataBetweenListenerAndClient | bestanden |
| LocalSocket.RejectsLiveOwnersAndReplacesStaleFiles | bestanden |
| NullPlatform.QueuesDrainsAndAppliesEventsDeterministically | bestanden |
| NullPlatform.SupportsHeadlessClipboardCursorMinimizeAndClose | bestanden |
| NullPlatform.BuffersTextAndDropEventsForEditorWorkflows | bestanden |
| NullPlatform.DeliversWindowFocusEventsInOrder | bestanden |
| PlatformLayering.SourceRules | bestanden |
| Strikter Layering-Check | 960 Dateien, 10.266 Import/Include- und 89 CMake-Link-Referenzen; keine Verletzungen |
| Dokumentationsscanner | 3 objektive Fehler, 7 Review-Hinweise; siehe PLAT12 |

Diese sieben vorhandenen Tests sind keine Abdeckung der neu beschriebenen Fehlerfälle.

| Isolierter Repro-Modus | Erwartung | Beobachtung am Audit-Commit |
| --- | --- | --- |
| held | Gehaltene Taste/Maus erzeugen Flanke | Exit 0, beide Flanken true |
| small | Kleiner Socket-Roundtrip | Exit 0 |
| tap | Down/Up innerhalb eines Polls behält Press-Flanken | Assertion scheitert; beide Flanken false |
| blocked-send | SendAll mit 20 ms liefert Timeout | Nach 1 s vom äußeren Watchdog beendet |
| probe-error | Fehlgeschlagene Probe erhält aktiven Pfad | Assertion scheitert; Pfad entfernt |
| replacement | Close erhält zwischenzeitliche Ersatzdatei | Assertion scheitert; Datei entfernt |
| callback-queue | Folgeevent bleibt bis zum nächsten Poll erhalten | Assertion scheitert; ShouldClose bleibt false |

Die Assertions formulieren die gewünschte Semantik vor der Ausführung. Es wurden keine Produktionsfixes geschrieben oder mutiert; die Repros sind noch keine registrierten Regressionstests. Ein anfänglich zu langer absoluter Unix-Socket-Pfad wurde in der Fixture auf einen relativen Pfad im isolierten Arbeitsverzeichnis korrigiert. Ein nicht ausgeführter experimenteller Repro-Modus namens wait wurde nach Peer-Kritik entfernt und wird nicht als Befund geführt. Alle sieben verbleibenden Modi wurden danach erneut ausgeführt, mit den Ergebnissen oben.

Nicht ausgeführt: vollständige CPU-Suite, ASan/UBSan, native GLFW-Minimize/Restore-Tests, GUI-/Vulkan-/HiDPI-Läufe, Nicht-Unix-Builds und Ende-zu-Ende-Shutdown des Sandbox-Prozesses.

### Bereits rote CI außerhalb dieses Befundumfangs

- [Main Linux-Clang-Lauf](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38869189381): Capture CPU test selection scheitert an RenderWorldPoolEngineWiring.SourceRules ohne registrierten Producer. Der fehlende test.json-Timingreport ist eine Folge. Separat erfasst in [BUG-244](../../tasks/done/BUG-244-cpu-selection-rejects-source-rules-producer.md).
- [Nightly-Lauf](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38854737500): bereits rot; separater Backend-/Manifest-Abgleich in [BUG-245](../../tasks/done/BUG-245-nightly-smoke-backend-manifest-mismatch.md). In diesem Audit wurde kein Benchmark ausgeführt.
- [ci-docs am Audit-Commit](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38869189384): grün.

Keine dieser Beobachtungen beweist einen zusätzlichen Platform-Testfehler.

## Reviews und Nachvollziehbarkeit

Drei unabhängige Rollen prüften read-only: Editor/Input-Integration, Agent-Client/Betrieb und Backend-/Headless-Wartung. Der Backend-Reviewer bestätigte die Ausrichtung auf die vier Nutzerfragen und die Beibehaltung der Struktur. Der Koordinator führte die oben genannten Builds und Repros aus.

Zusätzlicher Peer nach globalem peer-Skill: Codex → Claude Code, angefordertes und vom Ergebnis ausgewiesenes Modell claude-opus-5-5, angeforderter Effort max, Session 31ba3990-ab6d-488d-a27a-ff82b3a64820. Read/Grep/Glob waren die einzigen Werkzeuge; strikte MCP-Konfiguration und fehlende Shell-/Agent-Werkzeuge erzwangen read-only und keine weitere Delegation. Das Modell hat Hashes nicht selbst nachgerechnet.

- Geprüfter src/platform-Digest: SHA-256 ce8ec4e49d20e1518006d575f2b31dd56306cf3ea4bd3d425a0771607af4d278. Berechnung: alphabetisch sortierte relative Dateipfade unter src/platform, jeweils UTF-8-Pfad + NUL + Dateibytes + NUL.
- Initialer Packet-Digest: e77e5917b284c2b21aa6816aad7609fc1b4e4a13322aba44cc1b44752af49de7.
- Exchange-Digest: 47a9c6208a2ad20ded666119f10c38c2675a9c31041cba08020bceef10be43da.
- Wortlaut initial: „Alignment: approve.“
- Wortlaut der Delta-Prüfung: „Urteil zur Darstellung der Evidenz: revise required, ein Punkt. Danach approve, ohne weitere Runde.“
- Pflichtkorrektur: Den nicht ausgeführten wait-Modus entfernen oder separat belegen. Der Koordinator entfernte ihn und wiederholte alle verbleibenden Modi. Das ist die dokumentierte Erfüllung der Bedingung, kein erfundenes nachträgliches Reviewer-Urteil.
- Die Gegenprüfung bestätigte die Socket-Fehler, Eventverlust, ungeleerte Events, Kopierbarkeit, Cursorflags und Nachrichtengrenze. Sie verlangte die Unterscheidung zwischen isolierter Reproduktion und nicht ausgeführtem Runtime-/GUI-Pfad. Bei X11 bleibt die native Bestätigung offen.
- Finaler Repro-Quelltext-Digest nach der Korrektur: d991d315d7f28e6425556c2db363525499376f1d6575837357d4580a8f71b8a1.

Raw-Logs und Review-Transkripte sind temporäre Sitzungsevidenz. Dieser Bericht enthält die dauerhaften Befunde, Grenzen und vollständigen Repro-Rezepte; zum Weiterarbeiten ist kein Zugriff auf den ursprünglichen Chat oder dessen Scratch-Verzeichnis erforderlich.

## Empfohlene Umsetzung in getrennten Schritten

Dies sind Vorschläge für einen Folgeauftrag, keine bereits umgesetzten oder pauschal freigegebenen Änderungen. Vor Beginn aktuellen Git-Status, CI und die geltenden Repository-Verträge lesen. Bei abweichendem HEAD die Befunde anhand der Symbole erneut prüfen.

| Schritt | Befunde und Fragen | Kleinster vollständiger Umfang | Fertigkriterium |
| --- | --- | --- | --- |
| 1 Socket-Fehler | PLAT01–PLAT03, Frage 4 | LocalSocket und seine Tests; Runtime-Folge als Check | Alle drei zuvor fehlschlagenden Repros bestehen; bestehende Roundtrips und Ownership-Tests bleiben grün |
| 2 Eingaben und Null-Batch | PLAT04, PLAT07, Frage 4 | Input/Null plus betroffene direkte Verbraucher | Tap und Callback-Folgeevent bestehen; Hold-/Scroll-Verhalten bleibt erhalten |
| 3 Event-Lebensdauer | PLAT05, Fragen 1 und 4 | Runtime-Frameabschluss und betroffene UI-/No-UI-Verträge | Kein dauerhafter Restbestand ohne UI; UI-/Minimized-Ereignisse gehen nicht verloren |
| 4 Native Minimierung | PLAT06, Frage 4 | X11-Repro vor Verhaltensänderung; Wayland-Grenze ausdrücklich halten | Nativer Minimize/Restore-Test belegt Idle-Verhalten |
| 5 Kleine Runtime-Korrekturen | PLAT09–PLAT10, Frage 4 | Mit Runtime-Audit deduplizieren, getrennte kleine Fixes | Fähigkeitsflags und Nachrichtengrenze werden durch gezielte Tests getragen |
| 6 Ownership und Cleanup | PLAT08, PLAT11–PLAT12, Fragen 1 bis 4 | Copy-Sperre, tote API inklusive Testdoubles, Doku | Compile-time-Checks, betroffene Tests und Dokumentationschecks grün |

Tests zuerst gegen den unfixed Zustand zeigen und anschließend mit dem Fix bestehen lassen. Kein Grenzwert, Gate oder Test darf bloß zur Grünfärbung abgeschwächt werden. Bekannte Grenzen nicht still in neue Fähigkeiten umdeuten. Bei neuen Fehlerlagen außerhalb des jeweiligen Schritts Scope neu abstimmen.

Aktuelle Testbesitzer aus build/ci/test-inventories/RegisteredTestSources.tsv:

| Quelle | Executable |
| --- | --- |
| [Test.LocalSocket.cpp](../../tests/unit/platform/Test.LocalSocket.cpp), [Test.NullPlatform.cpp](../../tests/unit/platform/Test.NullPlatform.cpp) | IntrinsicPlatformTests |
| [Test.ImGuiAdapter.cpp](../../tests/contract/runtime/Test.ImGuiAdapter.cpp), [Test.RuntimeInputActions.cpp](../../tests/contract/runtime/Test.RuntimeInputActions.cpp), [Test.SceneInteractionModule.cpp](../../tests/contract/runtime/Test.SceneInteractionModule.cpp) | IntrinsicRuntimeContractTests |
| [Test.SandboxAgentServer.cpp](../../tests/integration/runtime/Test.SandboxAgentServer.cpp) | IntrinsicSandboxEditorIntegrationTests |
| [Test.GlfwPlatformSmoke.cpp](../../tests/integration/platform/Test.GlfwPlatformSmoke.cpp) | Im aktuell gewählten GLFW-fähigen Preset neu aus dessen Registry ermitteln; nicht im headless ci-Testinventar registriert |

Nur betroffene Targets und Suites bauen/laufen lassen. Bei weitreichender Änderung einer importierten Oberfläche den nach Repositoryvertrag erforderlichen größeren Umfang vorher begründen. Native GUI- und Sanitizer-Nachweise sind getrennte Evidenzklassen.

## Reproduktionsanhang

Der folgende C++-Quelltext wurde gegen die unveränderten, mit dem ci-Preset gebauten Module gelinkt. Er ist ein Linux-Audit-Harness, kein bereits integrierter Test. In einen lokalen temporären Ordner unter tasks/evidence/src-platform-audit-2026-10-16/ als platform_repro.cpp kopieren; nicht als neue Produktionsbibliothek übernehmen. Die Socket- und FD-Manipulationen bleiben in isolierten Kindprozessen und temporären Verzeichnissen.

Die Modmaps und Archive entstehen durch den oben genannten Build. Aus build/ci wurde mit folgendem Schema kompiliert; der Audit verwendete /bin/clang++-23:

    /bin/clang++-23 -std=c++23 -g -fno-exceptions -fno-rtti \
      @tests/CMakeFiles/PlatformTestObjs.dir/unit/platform/Test.NullPlatform.cpp.o.modmap \
      @tests/CMakeFiles/PlatformTestObjs.dir/unit/platform/Test.LocalSocket.cpp.o.modmap \
      ../../tasks/evidence/src-platform-audit-2026-10-16/platform_repro.cpp \
      lib/libExtrinsicPlatform.a lib/libExtrinsicCore.a \
      -o ../../tasks/evidence/src-platform-audit-2026-10-16/platform_repro

Vor Wiederholung den Compiler aus CMakeCache prüfen; eine andere Modul-Compiler-Version darf nicht mit diesen PCMs gemischt werden. Assertions eingeschaltet lassen.

```cpp
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <unistd.h>
import Extrinsic.Core.Config.Window;
import Extrinsic.Platform.Backend.Null;
import Extrinsic.Platform.Input;
import Extrinsic.Platform.Window;
import Extrinsic.Platform.LocalSocket;

int main(int argc, char** argv) {
    assert(argc == 3);
    namespace P = Extrinsic::Platform;
    const std::string_view mode = argv[1];
    if (mode == "callback-queue") {
        P::Backends::Null::NullWindow window{Extrinsic::Core::Config::WindowConfig{}};
        window.Listen([&window](const P::Event&) { window.QueueClose(); });
        window.QueueKey(P::Input::Key::F, true);
        window.PollEvents();
        window.Listen({});
        window.PollEvents();
        // A callback-enqueued close must not disappear before the next poll.
        assert(window.ShouldClose());
        return 0;
    }
    if (mode == "held" || mode == "tap") {
        P::Backends::Null::NullWindow window{Extrinsic::Core::Config::WindowConfig{}};
        const P::IWindow& view = window;
        window.QueueKey(P::Input::Key::F, true);
        window.QueueMouseButton(0, true);
        if (mode == "tap") {
            window.QueueKey(P::Input::Key::F, false);
            window.QueueMouseButton(0, false);
        }
        window.PollEvents();
        const bool key = view.GetInput().IsKeyJustPressed(P::Input::Key::F);
        const bool mouse = view.GetInput().IsMouseButtonJustPressed(0);
        std::fprintf(stderr, "key_edge=%d mouse_edge=%d key_held=%d\n", key, mouse,
                     view.GetInput().IsKeyPressed(P::Input::Key::F));
        // A press during this frame must survive a release in the same poll.
        assert(key && mouse);
        return 0;
    }
    const std::string path = argv[2];
    P::LocalSocketListener listener;
    assert(listener.Listen(path) == P::LocalSocketStatus::Ok);
    if (mode == "probe-error") {
        const rlimit limit{64,64};
        assert(::setrlimit(RLIMIT_NOFILE, &limit) == 0);
        while (::open("/dev/null", O_RDONLY) >= 0) {}
        P::LocalSocketListener contender;
        assert(contender.Listen(path) == P::LocalSocketStatus::SystemError);
        // Resource exhaustion in the contender must preserve the live owner.
        assert(std::filesystem::exists(path));
        return 0;
    }
    if (mode == "replacement") {
        assert(std::filesystem::remove(path));
        { std::ofstream replacement(path); replacement << "replacement"; }
        listener.Close();
        // Closing an old socket must not delete a different file at its path.
        assert(std::filesystem::exists(path));
        std::filesystem::remove(path);
        return 0;
    }
    P::LocalSocketConnection client, server;
    assert(P::ConnectLocalSocket(path, client) == P::LocalSocketStatus::Ok);
    assert(listener.Accept(server, 20) == P::LocalSocketStatus::Ok);
    if (mode == "small") {
        assert(server.SendAll("ok", 20) == P::LocalSocketStatus::Ok);
        std::string data;
        assert(client.Receive(data, 20) == P::LocalSocketStatus::Ok);
        assert(data == "ok");
        return 0;
    }
    assert(mode == "blocked-send");
    std::fprintf(stderr, "calling SendAll(8 MiB, 20 ms), peer does not read\n");
    // A stalled write must honor its explicit timeout; parent enforces 1 s.
    const auto status = server.SendAll(std::string(8 << 20, 'x'), 20);
    std::fprintf(stderr, "SendAll returned %s\n", P::ToString(status));
    assert(status == P::LocalSocketStatus::Timeout);
}
```

Der ursprüngliche Watchdog verwendete dieselbe subprocess.run-Timeout-Mechanik wie das folgende normalisierte Runner-Rezept. Das Rezept nach Ablage und Kompilierung aus dem Repositoryroot ausführen. Ein erfolgreicher späterer Fix muss bei allen sieben Modi Exit 0 liefern; der heutige Istzustand steht in der Ergebnistabelle.

```python
import pathlib
import resource
import subprocess
import tempfile

root = pathlib.Path("tasks/evidence/src-platform-audit-2026-10-16").resolve()
assert (root / "platform_repro").is_file()
# Auf Linux erzeugen die erwarteten Assertion-Fehler keine Core-Dumps.
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
for mode in ("held", "tap", "small", "blocked-send",
             "replacement", "probe-error", "callback-queue"):
    with tempfile.TemporaryDirectory(prefix="case-", dir=root) as case:
        try:
            result = subprocess.run(
                [str(root / "platform_repro"), mode, "test.sock"],
                cwd=case, capture_output=True, text=True, timeout=1)
            print(mode, "exit", result.returncode)
            print(result.stderr)
        except subprocess.TimeoutExpired as error:
            print(mode, "WATCHDOG TIMEOUT after 1 second")
            print(error.stderr)
```
