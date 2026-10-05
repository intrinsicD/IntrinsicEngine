---
id: REVIEW-007
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interaktive Review-Planung auf ausdrücklichen Operator-Wunsch; Kandidatendossiers, getrennte Claude- und Codex-Reviews und ausdrückliche menschliche Entscheidungen werden in diesem Task festgehalten. Dieser Task autorisiert weder eine Umsetzung noch ein Forschungsergebnis.
contract_schema: 1
contracts: []
contract_review: Den Contract-Katalog geprüft. Dieser Task sammelt und prüft Vereinfachungshypothesen, ohne Engine-Verträge, Quellschnittstellen, Tests, Formate oder repository-weite Workflow-Regeln zu ändern. Die Backlog-README erhält nur einen Navigationslink. Jeder freigegebene Umsetzungstask muss seine zutreffenden Contracts vor der Umsetzung selbst ermitteln und deklarieren.
---
# REVIEW-007 — Ponytail-Audit: Funde zerlegen und einzeln mit Operator, Claude und Codex entscheiden

## Goal

Alle Funde der beiden repository-weiten Ponytail-Audits vom 2026-10-03
(Claude-Audit und Codex-Audit auf derselben Revision) in kleine, einzeln
entscheidbare Prüfkandidaten zerlegen. Jeden Kandidaten anschließend mit dem
Operator, Claude und Codex prüfen. Erst die ausdrückliche Entscheidung des
Operators bestimmt, ob daraus ein tatsächlicher Umsetzungstask entsteht, der
bestehende Code erhalten bleibt, der Fund verworfen wird oder die Frage
zurückgestellt wird.

Das Ergebnis ist eine nachvollziehbare Entscheidungsliste mit begründeten
Einzelurteilen und ausschließlich den vom Operator ausgewählten Folgetasks.
Eine bestimmte Zahl gelöschter Zeilen oder erzeugter Tasks ist kein Ziel.

## Context

- Auftrag vom 2026-10-03: einen ausführlichen übergeordneten Task erstellen;
  dieser soll zuerst die Audit-Funde aufteilen und dann jeden einzelnen
  gemeinsam mit Operator, Codex und Claude prüfen. Die abschließende Auswahl
  bleibt beim Operator.
- Entstehung: Claude und Codex haben parallel je einen Audit und je eine
  Planungsnotiz angelegt, [REVIEW-005](../../done/REVIEW-005-ponytail-overengineering-audit-triage.md)
  (Claude, breites Inventar mit 123 Zeilen) und
  [REVIEW-006](../../done/REVIEW-006-ponytail-candidate-triage-and-human-decisions.md)
  (Codex, zwölf Gruppen PK01–PK12 mit strengerem Prozess). Auf Wunsch des
  Operators sind beide hier zusammengeführt und als ersetzte Planungsnotizen
  retired. Dieser Task übernimmt den Prozess, die Grenzen, das Dossierformat
  und die Prüfhinweise aus REVIEW-006 sowie das vollständige Inventar aus
  REVIEW-005. Überschneidende Funde stehen als eine Zeile im Inventar.
- Status bei Anlage: **geplant; Zerlegung und Einzelreviews noch offen**.
  Die Anlage dieser Notiz startet keine Kandidatenprüfung und keine Umsetzung.
- Ausgangsrevision beider Audits: `0ebb2f4503fd3ee36c52fce73f3533345802f844`.
  Der Checkout war beim Audit unverändert. Ein späterer Review muss die dann
  tatsächlich betrachtete Revision und gegebenenfalls den Diff festhalten.
- Claude-Audit: Skill `ponytail-audit` (lokales Plugin
  `~/.claude/plugins/marketplaces/ponytail`) in sechs lesenden Teil-Audits:
  Tools/Infra, Core/ECS/Assets/Platform, Graphics, Runtime ohne Editor/Modules,
  Runtime-Editor/Modules plus App, Geometry. `tests/` war ausgenommen.
  `ponytail-debt` fand 0 `ponytail:`-Marker.
- Codex-Audit: statischer Audit der getrackten Engine-Quellen, Shader, Tests,
  Benchmarks und Werkzeuge mit Referenzsuche und ausgewählten Quelltext-
  vergleichen. Keine Löschung wurde erprobt; kein Build oder Test für die
  vorgeschlagenen Änderungen ausgeführt.
- Ponytail prüft nur Überkomplexität (`delete`, `stdlib`, `native`, `reuse`,
  `yagni`, `shrink`). Korrektheit, Sicherheit und Performance liegen außerhalb.
- Die Tags sind Hypothesen. Insbesondere bedeutet ein fehlender direkter Import
  noch nicht, dass ein Symbol, eine Datei oder ein Shader entbehrlich ist.
  „Nur von Tests benutzt“ ist nicht dasselbe wie tot: Geometry und Core sind
  teilweise Forschungsbibliothek, und ein Modul kann Vorrat für eine geplante
  Methode oder einen offenen Task sein.
- Die Zeilenschätzungen (Claude-Audit gesamt etwa −38.000 Zeilen und −2
  vcpkg-Abhängigkeiten; Codex-Audit etwa −2.800 Dateizeilen) sind
  unbestätigt, überschneiden sich teilweise und sind weder Budget noch
  Leistungsnachweis.
- Bei der Anlage stichprobenartig bestätigt: `Core.RingBuffer` (0 Importe),
  `Core.Process` (nur Tests), Telemetry ohne Leser, `RenderArtifactRegistry`
  ohne Registrierung im Produktivcode, `JobService::CancelAll` ohne Aufrufer,
  `Graphics.GpuScene` ohne Importe (`GpuSceneSlot` ist ein anderes Modul),
  die Pick-/Point-Shader ohne Codereferenz, imguizmo/draco ohne Nutzung in
  `src`/`tests`, VectorHeatMethod/ConvexHullBuilder/RotationAveraging/
  HtexPatch/Graph.ShortestPath nur von ihrem eigenen Unit-Test importiert,
  `CreateVulkanSurface` ohne Aufrufer, die PK05-Typen und
  `CachedSelected*Indices` nur mit ihrer Deklaration, `PathKey` ohne
  Verbraucher, die alten Leseschlüssel in `Runtime.SceneSerialization.cpp:1276-1298`.
- Diese Arbeit ist eine ausdrücklich beauftragte Architekturprüfung und ändert
  die stehende Framework24-Priorität und die Gates von
  [REVIEW-004](REVIEW-004-framework24-product-convergence-audit.md) nicht.
- Der Task wird erst bei Beginn der interaktiven Bearbeitung nach
  `tasks/active/` verschoben. Ein Eintrag als unblocked im Session-Brief
  bedeutet nur, dass keine Task-Abhängigkeit fehlt; er gibt keine automatische
  Umsetzung oder unbeaufsichtigte Abarbeitung frei.

## Überschneidungen mit offenen Tasks

Vor jeder Entscheidung prüfen; bevorzugt in den bestehenden Task einordnen,
statt einen doppelten Task anzulegen:

- `GEOM-089` (Heat-Methoden) → GE03; `METHOD-048` (HKTex) → GE03, GE07.
- `UI-046`, `RUNTIME-282`, `RUNTIME-283` (Export) → GE01, GE11 (`Geometry.IO`).
- `CI-014` und die Theme-H-Reihe `CI-012`…`CI-020` → T02, T06, T07, T08.
- `GEOM-115` (Umbenennung der Halfedge-Module) → GE08, GE11, GE20, GE21.
- Aktive `METHOD-043`/`METHOD-044`/`METHOD-045` (Curvature-/Atlas-
  Experimente) → T03, T04.

## Autorisierung und Grenzen

1. **Dieser Task autorisiert Analyse und Review-Dokumentation.** Er autorisiert
   weder Löschungen noch Refactorings, Änderungen an Tests, Shadern, CMake,
   Laufzeitverhalten, öffentlichen APIs oder Szenenformaten.
2. **Prüfkandidaten sind noch keine Umsetzungstasks.** Sie erhalten lokale
   Kennungen innerhalb dieser Notiz. Sie bekommen zunächst keine eigene
   Repository-Task-ID, keinen Claim und keinen Implementierungsauftrag.
3. **Jeder Kandidat wird einzeln geprüft.** Ein Votum für einen Shader oder
   Typ gilt nicht für benachbarte Dateien. Eine Gruppe darf nur gemeinsam
   entschieden werden, wenn ihre Unteilbarkeit begründet und mit dem Operator
   abgestimmt ist.
4. **Codex und Claude geben getrennte technische Einschätzungen ab.** Keine
   der beiden Einschätzungen ist eine Freigabe. Zwei zustimmende Agenten
   ersetzen nicht die menschliche Entscheidung.
5. **Nur der Operator entscheidet abschließend.** Schweigen, Zeitablauf,
   ein zustimmendes Agentenreview oder die ursprüngliche Audit-Liste gelten
   nicht als Zustimmung zur Task-Erstellung oder Codeänderung.
6. **Eine Freigabe zur Task-Erstellung ist keine Freigabe zur Umsetzung.**
   Der erzeugte Task bleibt zunächst im Backlog. Eine gleichzeitig erteilte,
   ausdrückliche Implementierungsanweisung wird getrennt dokumentiert.
7. Das Erhalten des gesamten geprüften Codes ist ein zulässiges Endergebnis.
   Widerlegte Funde werden korrigiert, statt durch neue Begründungen an der
   ursprünglichen Löschidee festzuhalten.

## Ablauf

### Phase A — Funde in kleine Prüfkandidaten zerlegen

- [ ] Aktuelle Revision, Arbeitsbaumzustand und offene Arbeiten im betroffenen
  Bereich aufnehmen; die Quellen gegenüber der Audit-Ausgangsrevision prüfen.
- [ ] Jede Inventarzeile unten in konkrete, möglichst unabhängige
  Entscheidungsfragen zerlegen. Sammelzeilen (z. B. G01 Shader, GE11 Module,
  G10/R12/R17 Methodenlisten, X04 `DebugNameFor*`) nicht ungeprüft als
  Zuschnitt eines Umsetzungstasks übernehmen.
- [ ] Jedem Kandidaten eine lokale Kennung geben, abgeleitet von seiner
  Inventarzeile (z. B. `G01-03`, `PK05-02`), und exakte Dateien/Symbole sowie
  den Owner nennen. Lokale Kennungen dürfen nicht als `depends_on`-Task-IDs
  verwendet werden.
- [ ] Für jeden Kandidaten eine konkrete Frage formulieren, zum Beispiel:
  „Kann dieser Shader entfallen, ohne einen aktuellen Pipeline-, Config- oder
  Testpfad zu verlieren?“ Keine vorentschiedenen Titel wie „Shader löschen“.
- [ ] Abhängigkeiten und Überschneidungen zwischen Kandidaten kennzeichnen.
  Insbesondere `GpuScene`, Culling, Shader, ECS-Proxies und die
  Querschnittszeilen X01–X04 nicht mehrfach als dieselbe Einsparung zählen
  oder gegenseitig als unbelegte Ersatzpfade benutzen.
- [ ] Dem Operator die zerlegte Liste und eine sinnvolle Prüfreihenfolge
  zeigen. Offene Zuschnittsfragen vor der jeweiligen Detailprüfung klären.

Für die erste Zerlegung gelten diese Leitplanken:

- Shader zunächst **pro Datei** inventarisieren; ein Vertex-/Fragment-Paar
  kann danach mit Begründung als ein gemeinsam zu entscheidender Kandidat
  vorgeschlagen werden. Keine pauschale Entscheidung über alle Dateien.
- Tote Module und Modulpartitionen nach Partition und tatsächlicher
  Typverwendung aufteilen. Unverwendete Typen und nötige transitive Exporte
  getrennt prüfen.
- Die drei Selection-Cache-Typen jeweils sichtbar machen; die Culling-Proxy-
  Komponente bildet einen eigenen Kandidaten.
- Bei Culling die veraltete Registrierungs-API und den aufgerufenen leeren
  Synchronisationsschritt getrennt betrachten. Ihre Test- und Diagnostikfolgen
  müssen vor einem möglichen Zusammenfassen geklärt sein.
- Bei Duplikaten zusammengehörige Funktionen nur dann bündeln, wenn dieselben
  Aufrufer, dieselbe Semantik und derselbe sinnvolle gemeinsame Owner vorliegen.
- „Nur von Tests benutzt“ immer gegen offene Tasks, Methodenpläne und `ara/`
  prüfen, bevor ein Kandidat als tot gilt.

### Phase B — Einen Kandidaten gemeinsam prüfen

Für jeden Kandidaten denselben kurzen, überprüfbaren Ablauf durchlaufen. Der
Agent, der die Sitzung führt (Claude oder Codex), ist der **Bearbeiter**; der
jeweils andere ist der **Gegenprüfer**.

1. **Der Bearbeiter ermittelt den Ist-Zustand.** Definitionen, Aufrufer,
   Re-Exports, Stringnamen, dynamische Pfadbildung, Build-Einträge,
   Config-/Szenenwerte, Fixtures, Tests und aktive Task-Bezüge suchen.
   Historische Fundstellen getrennt von aktuellen Verbrauchern bewerten.
2. **Der Bearbeiter erstellt ein Dossier.** Zweck, heutige Verwendung,
   konkrete Vereinfachung, Alternative „behalten“, Risiken und fehlende Evidenz
   mit Quellverweisen festhalten. Bei Wiederverwendung prüfen, ob beide
   Verträge tatsächlich zusammenpassen und ob ein gemeinsamer Helper weniger
   Aufwand verursacht als die vorhandene begrenzte Dopplung.
3. **Der Gegenprüfer prüft denselben Stand separat.** Claude über die echte
   Claude-Code-CLI bzw. Codex über den konfigurierten Codex-Zugang. Den Auftrag
   auf diesen Kandidaten begrenzen und ausdrücklich nach Gegenbelegen und
   Gründen zum Behalten suchen lassen. Kein vom Bearbeiter erfundenes Votum
   des Gegenprüfers.
4. **Die beiden Einschätzungen gegenüberstellen.** Übereinstimmungen,
   Unterschiede, Unsicherheiten und gegebenenfalls notwendige zusätzliche
   Nachweise sichtbar machen. Technische Uneinigkeit durch Quellen oder
   passende Prüfungen bearbeiten; sie nicht per Agentenmehrheit auflösen.
5. **Mit dem Operator besprechen.** Die konkrete Entscheidungsvorlage für
   genau diesen Kandidaten zeigen. Fragen, Einwände und gewünschte Änderungen
   des Operators in das Dossier aufnehmen.
6. **Menschliche Entscheidung festhalten.** Wortlaut oder bestätigte
   Zusammenfassung, Datum, betrachteten Stand und etwaige Bedingungen notieren.
   Erst danach zum nächsten entscheidungsreifen Kandidaten übergehen, sofern
   der Operator keine andere Reihenfolge oder Bündelung wünscht.

Der Bearbeiter kann unabhängig von einer ausstehenden Entscheidung weitere
Quellen lesen und Unterlagen vorbereiten. Das erteilt keine Entscheidung für
den wartenden Kandidaten und keine Erlaubnis, Folgeimplementierungen zu starten.

### Phase C — Nur ausgewählte Umsetzungstasks konkretisieren

- [ ] Für jeden ausdrücklich ausgewählten Kandidaten einen kleinen Task mit
  genau einer Absicht vorbereiten. Scope, Nicht-Ziele, Dateien, notwendige
  Änderungen, Risiken und konkret passende Prüfungen aus dem Dossier übernehmen.
- [ ] Den Contract-Katalog für den tatsächlichen Implementierungsumfang erneut
  prüfen und die zutreffenden IDs deklarieren. Der leere Contract-Satz dieser
  übergeordneten Review-Notiz darf nicht auf Codeänderungen übertragen werden.
- [ ] Bereits existierende aktive oder geplante Aufgaben auf Überschneidung
  prüfen. Dem Operator gegebenenfalls eine Einordnung in einen vorhandenen
  Task vorschlagen, statt automatisch einen doppelten Task zu erzeugen.
- [ ] Neue IDs erst nach der menschlichen Auswahl und nach Prüfung aller
  Task-Lifecycle-Verzeichnisse vergeben. Die fertige Task-Datei mit Kandidat
  und Entscheidung in beide Richtungen verknüpfen.
- [ ] Bei `behalten`, `widerlegt` oder `vertagen` die jeweilige Begründung
  dokumentieren. Daraus keine automatische Cleanup-, Hint- oder Wiedervorlage-
  Aufgabe machen; eine Wiedervorlage benötigt einen genannten Anlass.
- [ ] Nach Änderungen an offenen Task-Dateien den Session-Brief regenerieren
  und die strukturellen Prüfungen ausführen.

## Rollen

- **Operator:** prüft jeden Kandidaten mit, bestimmt die Reihenfolge und trifft
  die abschließende Entscheidung. Kann weitere Nachweise verlangen, den
  Zuschnitt ändern oder den Kandidaten ohne Umsetzung beenden.
- **Bearbeiter (Claude oder Codex):** recherchiert, erstellt die prüfbare
  Vorlage, gibt sein eigenes Votum ab, koordiniert das separate Review des
  Gegenprüfers und dokumentiert die menschliche Entscheidung. Ist für die
  korrekte Wiedergabe beider Reviews und für die Begrenzung auf den
  freigegebenen Umfang verantwortlich.
- **Gegenprüfer (der jeweils andere Agent):** unabhängiger technischer
  Reviewer des einzelnen Kandidaten; prüft auch den Fall, dass die
  Ausgangshypothese falsch ist. Nennt konkrete Quellstellen, notwendige
  bestehende Grenzen und offene Evidenz.

Für die Claude-Delegation gilt die
[stehende Autorisierung](../../../AGENTS.md#standing-claude-code-authorization).
Der Auftrag an den Gegenprüfer ist auf lesende Kandidatenreviews begrenzt.
Geheimnisse, Authentifizierungsdaten und sachfremde persönliche Daten gehören
nicht in Review-Pakete. Beide Agenten betrachten denselben festgehaltenen
Quellstand; der Gegenprüfer erhält keinen Schreibauftrag auf diesem Checkout.
Den Umfang und die verwendete Revision des Pakets sowie das tatsächliche
Ergebnis im Dossier festhalten. Wenn sinnvoll, erhält der Gegenprüfer zuerst
die neutrale Fragestellung und Quellen und erst anschließend das Votum des
Bearbeiters.

Ist ein Agent nicht verfügbar, bleibt dessen Review offen. Kein anderer
Agentenname und kein selbst formulierter Konsens ersetzt ihn. Vorliegende
Recherche kann weiter vorbereitet werden; die fehlende Beteiligung wird dem
Operator genannt. Jede vom Operator gewünschte Abweichung wird ausdrücklich
als solche dokumentiert.

## Etappen

Eine Etappe pro Sitzung genügt; die Reihenfolge kann der Operator ändern.

- [ ] E0 — Codex-Audit-Gruppen ohne Gegenstück (PK03–PK12)
- [ ] E1 — Querschnittsduplikate (X01–X04)
- [ ] E2 — Tools, CI, Abhängigkeiten (T01–T23)
- [ ] E3 — Core, ECS, Assets (C01–C17)
- [ ] E4 — Graphics (G01–G18)
- [ ] E5 — Runtime ohne Editor/Modules (R01–R19)
- [ ] E6 — Runtime-Editor/Modules und Sandbox-App (E01–E17)
- [ ] E7 — Geometry (GE01–GE25)
- [ ] E8 — Konsolidierung nach Phase C: freigegebene Folgetasks anlegen
  (Kandidaten mit gleichem Owner gebündelt, wenn der Operator zustimmt),
  Einordnungen in bestehende Tasks vermerken, Session-Brief regenerieren.

## Inventar

Alle Einträge sind **offene Hypothesen**, keine bestätigten Löschfreigaben.
Spalten: Tag = Ponytail-Tag; Schätz. = Audit-Schätzung entfernbarer Zeilen
(unbestätigt); Claude / Codex = Votum des jeweiligen Agenten
(`bestätigt` / `teilweise` / `widerlegt`, mit Verweis auf das Dossier);
Operator = Entscheidung (siehe §„Zulässige Entscheidungen“) mit Folgetask.
`—` = offen. Die Fundbeschreibungen sind aus den Audits übernommen
(überwiegend englisch, wie im Quelltext). Zeilen mit `(PKxx)` vereinen einen
Claude-Fund mit einer Codex-Gruppe; die Prüfhinweise zu PKxx stehen im
nächsten Abschnitt.

### X — Querschnittsduplikate
| ID | Fund | Tag | Schätz. | Claude | Codex | Operator |
|----|---------|-----|------|--------|--------|----------|
| X01 | ~11 hand-rolled FNV-1a loops → `Core::HashString64` (+ bytes/incremental overload). Note: 4 copies use a truncated offset basis `1469598103934665603` (should be `…6656037`): HalfedgeMesh.Utils.cpp:69, AssetWorkflowRecipePolicies.cpp:71, GeometryProcessingOperations.Normals.cpp:615, …PointProperties.cpp:85 | reuse | -60 | teilweise: 7 Kand.; −60 nicht erreichbar (≈−20 nur mit neuer Core-API); abgeschnittene Basis wirkungslos (nur in-process); GpuWorld/TextureBake-Fingerprints sind Vertrag → vertagen; FromPath → PK11 | — | — |
| X02 | 5 consteval `__PRETTY_FUNCTION__` type-name/token helpers (Core.Hash TypeSig, TaskGraph TypeTokenValue, Asset.TypePool, ServiceRegistry/KernelEvents/JobService/CommandBus) → one `Core::TypeName<T>()` / `TypeToken<T>()` (merges C13, R06) | reuse | -90 | teilweise: Asset.TypePool-Helfer existiert nicht; TaskGraph-Token bewusst constexpr (behalten); 4 Kernel-`*TypeNameOf` → Task (≈−34) | — | — |
| X03 | ~67 file-local `IsFinite*` helpers in geometry + 4 overloads in VisualizationRecipes → `Geometry::Validation::IsFinite` / `glm::isfinite` (merges GE16, R19) | reuse | -260 | teilweise: R19 in RUNTIME-314 entschieden; AABB/Sphere/Triangle → Importzyklus; X03-05 (Validation inline) Task, danach X03-04 je Modulfamilie (≈−160, perf-prüfen) | — | — |
| X04 | ~43 hand-written `DebugNameFor*` switches; many have 0 or test-only callers (merges R03, E02, E13, E14). Delete the uncalled ones first; table-driven helper is optional (loses `-Wswitch`) | delete/shrink | -300…-500 | teilweise: 10 ohne Aufrufer → Task in 3 Owner-Bündeln (≈−206); 11 nur-Test = Diagnose (behalten); Tabellenhelfer widerlegt | — | — |

### T — Tools, CI, Abhängigkeiten
| ID | Fund | Tag | Schätz. | Claude | Codex | Operator |
|----|---------|-----|------|--------|--------|----------|
| T01 | `tools/agentkit/` standalone product, 0 refs from CI/CMake/src/AGENTS.md | yagni | -2800 | teilweise: 0 Refs korrekt; Produkt-oder-Altlast ist Operatorfrage → vertagen | — | — |
| T02 | Module-aware ccache fingerprinting `tools/ci/ccache_ci.py` + `ccache_module_invalidation_probe.py` + `cmake/Dependencies.cmake:47-90` (overlap CI-016) | yagni | -2000 | bestätigt als Überlappung → einordnen CI-016/CI-020 (Korrektheitsmaßnahme gegen stale BMIs) | — | — |
| T03 | `tools/diagnostics/curvature/` one-off experiment scripts + round JSONs (neck_sweep*, shape_diameter_*, thickness_*, …) (check METHOD-043/044/045) | delete | -1700 | teilweise: ara-Claims, CI-Tests, C++-Probes binden → nur ungebundene Round-JSONs prüfen; Rest vertagen bis METHOD-043 | — | — |
| T04 | `tools/diagnostics/atlas/` collect/refine/trace scripts, keep `patch_merge.py`/`repack.py` (check METHOD-044/045) | delete | -2000 | teilweise: Tests importieren baseline_atlas/boundary_refine → zerlegen (trace_* prüfen); vertagen bis METHOD-044/045 | — | — |
| T05 | Coverage-cohort transition: `test_cohort_parity.py`, `test_cohort_manifest.py`, `slow_test_cohort.json` | delete | -700 | teilweise: Parity-Tool+Test+JSON → Task; Manifest-Zweig hängt an T06 | — | — |
| T06 | Hand-rolled source coverage stack (`source_coverage.py`, `run_source_coverage.py`, `compare_source_coverage.py`) → gcovr / `llvm-cov export` | native | -1500…-2500 | teilweise: nutzt bereits llvm-cov export; gcovr passt nicht → Vereinfachung zerlegen, Löschen vertagen | — | — |
| T07 | `tools/ci/touched_scope.py` (2081 lines) → path→gate table (overlap CI-014, CI-018) | yagni | -1200 | bestätigt als Überlappung → einordnen CI-014/CI-018/CI-020 (Löschen vor Soak verboten) | — | — |
| T08 | Gate-timing telemetry chain (`collect_test_timing`, `validate_gate_timing_baseline`, `aggregate_gate_timing`, `time_command`, latency baseline JSON) → one script | yagni | -1100 | bestätigt als Überlappung → einordnen CI-020; Baseline-Validator separat | — | — |
| T09 | Knowledge-graph tooling (`build_knowledge_graph.py`, `export_method_graph.py`, `export_module_graph.py`, `provision_knowledge_graph.sh`); MCP `graphify-mcp` not installed locally | delete | -750 | teilweise: MCP-Eintrag + provision_*.sh → Task; Python-Kern speist Draw-Architecture-Skill → behalten | — | — |
| T10 | `tools/analysis/benchmark_compile_iteration.py`, `compile_hotspot_post_pimpl.json`, dated build-time baseline md | delete | -500 | T10-01 widerlegt (Runner hash-gepinnt in ara); T10-02 MD+JSON (Pfad korrigiert: tools/) → Task | — | — |
| T11 | Shared Python helper (`load_json`, `write_json`, `_write_github_outputs`, `parse_args`, 19× `sys.path.insert`) → `tools/_lib/common.py` | reuse | -600 | teilweise: Helfer semantisch verschieden → vertagen bis T01–T10 entschieden | — | — |
| T12 | Merge `workflow_evidence` / `experiment_custody` / `agent_work_graph` validators (~8.8k lines + ~23k test lines); subjective, the 2026-07-17 validator-rent audit kept them | yagni | -3000 | widerlegt (Fable): reale 5.9k statt 8.8k, Tests 3.7k statt 23k; CI-genutzt; PROC-027 behielt → behalten | — | — |
| T13 | `tools/agents/mcp_bridge.py` → thin stdio proxy (real wiring, shrink only) | shrink | -300 | widerlegt: Protokoll-/Reconnect-/Deadline-Logik, kein reiner Proxy → behalten | — | — |
| T14 | Skill mirror machinery `sync_skills.py` / `resync_skills.sh`; optionally `render_architecture.py` | yagni | -250…-1400 | T14-01 widerlegt (CI + AGENTS.md-Policy); T14-02 → mit T09 entscheiden | — | — |
| T15 | `check_codex_config.py` hand YAML parser (file is `.toml`) → `tomllib` | stdlib | -100 | widerlegt: Datei ist `.codex/config.yaml` (YAML), tomllib passt nicht | — | — |
| T16 | Shell/py shims: `check_expected_top_level.py`, `tools/check_ui_contract_guard.sh`, `tools/benchmark/check_perf_regression.sh`, `check_todo_active_only.sh`, `run_repo_hygiene_checks.sh` | delete | -40 | teilweise: 3 Shims ohne CI-Aufrufer → Task; check_expected_top_level (Test) und run_repo_hygiene behalten | — | — |
| T17 | Vendored `tools/agents/patches/research-manager-2.1.0-relevance.patch` | yagni | small | widerlegt: Patch ist lokal aktiv angewendet, Reproduzierbarkeit → behalten | — | — |
| T18 | Merge curvature boundary/extrema viewers in `benchmarks/runners/` | shrink | -150 | widerlegt: gemeinsamer Teil bereits geteilt → behalten | — | — |
| T19 | Generate benchmark smoke manifests from one table (speculative) | shrink | -300 | widerlegt: Manifeste inhaltlich verschieden; Generator = zweite Wahrheitsquelle | — | — |
| T20 | vcpkg `draco` — 0 uses in src/tests, only linked into tinygltf | native | 1 dep | teilweise: TINYGLTF_ENABLE_DRACO aktiviert Draco-glTF-Laden → Operatorfrage, vertagen | — | — |
| T21 | vcpkg `imguizmo` — 0 uses in src/tests, still linked | native | 1 dep | bestätigt: `imguizmo_lib` nirgends gelinkt, 0 Nutzung → Task | — | — |
| T22 | vcpkg overlay ports xatlas/imgui — replaceable by registry features? | yagni | -190 | imgui-Overlay teilweise (vertagen, GPU-Smoke nötig); xatlas-Overlay widerlegt (Baseline hat keinen Port) | — | — |
| T23 | Untracked stale `.claude/worktrees/agent-*` full-repo copies (local hygiene, not a repo change) | delete | local | teilweise: KEINE stale Kopien — 3 Worktrees mit 4 ungemergten Commits; lokale Hygiene, Operator entscheidet | — | — |

### C — Core, ECS, Assets
| ID | Fund | Tag | Schätz. | Claude | Codex | Operator |
|----|---------|-----|------|--------|--------|----------|
| C01 | `Core.Telemetry` — nothing in src reads it; `EXTRINSIC_PROFILE_*` unused | delete | -550 | teilweise (Fable): Profile-Makros → Task; Alloc-Zähler widerlegt (AgentOperations liest); TelemetrySystem → vertagen (Policy Marker-Pflicht) | — | — |
| C02 | Coroutine `Tasks::Job`, WaitToken, Park/Unpark (0 co_await outside core) | delete | -450 | teilweise (Fable): Job/Awaiter/WaitToken-Pfad Löschkandidat nach SLO-Klärung; Worker-Park/Unpark widerlegt | — | — |
| C03 | `Core.Process` — test-only | delete | -325 | teilweise: RUNTIME-282 plant Nutzung → vertagen | — | — |
| C04 | `FileWatcher` / `Core.Filesystem` (not PathResolver) — test-only | delete | -238 | bestätigt: FileWatcher 0 Nutzer (PathResolver ausnehmen) → Task | — | — |
| C05 | `Memory::ScopeStack`, `ArenaMemoryResource` — test-only | delete | -230 | bestätigt: ScopeStack → Task (+Doku-Korrektur); ArenaMemoryResource → mit C06 | — | — |
| C06 | `LinearArena` + `ArenaAllocator` → `std::pmr::monotonic_buffer_resource` (GJK/EPA ignore `scratch` in places) | stdlib | -200 | teilweise: EPA nutzt Arena aktiv, Telemetry::Alloc hängt dran → zerlegen; toter GJK-`scratch`-Parameter → Task | — | — |
| C07 (PK06) | `Core::RingBuffer<T,N>` — 0 importers | delete | -66 | bestätigt: 0 Importe; README-Falschaussage mit korrigieren → Task | — | — |
| C08 | `Asset.OperationStatus` — test-only | delete | -165 | bestätigt → Task | — | — |
| C09 | `FrameGraph` wrapper over `Dag::TaskGraph` (options duplicate field-for-field) | yagni | -150 | C09-01 Options-Alias → Task (≈−15); Fassade trägt ECS-Vokabular → behalten | — | — |
| C10 | `CallbackRegistry<Sig,Tag>` — exactly one instantiation (Asset.Service) | yagni | -150 | widerlegt: Readiness-Report 2026-08-06 „retain“ (Stale-Token-Korrektheit) | — | — |
| C11 | `Core.Config.EngineLoad` per-field readers → table/`from_json` (low confidence; diagnostics deliberate) | shrink | -300 | teilweise: ≈−120…−180, Diagnose-Parität schützen → vertagen | — | — |
| C12 | `AssetEventBus` per-asset Subscribe/Unsubscribe — only SubscribeAll used | delete | -60 | bestätigt: per-Asset-Abo 0 Produktionsaufrufer → Task (Tests umschreiben) | — | — |
| C13 | Duplicate type-token generators → see X02 | reuse | (X02) | → X02 | — | — |
| C14 | `TaskGraphExecutionMode::PlanOnly` — legacy test only | delete | -60 | widerlegt: Bench_TaskGraphPlanReuseSmoke + 2 Baselines nutzen PlanOnly | — | — |
| C15 | `Core::Hash::U64Hash` — 0 users (culling proxy → PK10) | delete | -26 | bestätigt (trivial) → Mini-Task, bündelbar | — | — |
| C16 | `ECS.Events` (SelectionChanged, HoverChanged, …) — test/fixture only | delete | -37 | teilweise: Layering-Fixtures + Test.CheckLayering hängen daran → vertagen | — | — |
| C17 | Logging `GetEntryCount`, `GetSequenceNumber`, `LevelMask` — 0 callers | delete | -30 | GetEntryCount/SequenceNumber behalten; LevelMask widerlegt (DiagnosticsStream nutzt) | — | — |

### G — Graphics
| ID | Fund | Tag | Schätz. | Claude | Codex | Operator |
|----|---------|-----|------|--------|--------|----------|
| G01 (PK01) | 29 shaders no pipeline loads: the 20 PK01 files (pick_*, point_surfel/retained/flatdisc/sphere, scene_update, instance_cull_multigeo, debug_view.comp, deferred/gbuffer.vert) plus 8 root `line`/`point`/`triangle`/`debug_surface` `.vert/.frag` (only the `forward/` variants are loaded); GLOB still compiles them all | delete | -2300 | bestätigt, zerlegt: 28 Dateien (nicht 29). pick_* (404), point_* (868, ohne point_splat.glsl), 3 Compute (283) → Task; deferred/gbuffer.vert, root line/point/triangle, debug_surface → einordnen LEGACY-043 | — | — |
| G02 | `Graphics.SharedRenderRecipeExecution` + contract integration stats nobody reads | delete | -1450 | widerlegt: Contract-Prüfung bricht Frame ab (fail-closed), Tests + GPU-Smoke lesen | — | — |
| G03 | Legacy pre-GpuScene shaders surface.vert, deferred_lighting.frag, shadow_depth.vert (+ source-grep test assertions, shadow_sampling/surface_color_resolve glsl) | delete | -800 | bestätigt → einordnen LEGACY-043 (shadow_depth.vert ergänzen; .glsl erst danach) | — | — |
| G04 | ~40× pipeline-create/publish block in `InitializeOperationalPassResources` → one helper | shrink | -650 | teilweise: ≈−250…−400, Blöcke nicht gleichförmig → nach Familie zerlegen, gpu;vulkan nötig | — | — |
| G05 (PK02) | `Graphics.GpuScene` legacy wrapper — 0 importers | delete | -500 | bestätigt: 0 Importe, kein Test → Task | — | — |
| G06 | `IRenderer` (58 virtuals, 1 impl misnamed `NullRenderer`) → concrete class | yagni | -250 | bestätigt (Fable): Rename NullRenderer→Renderer, dann Interface kollabieren → Task (zweistufig) | — | — |
| G07 | `RenderSubsystemRegistry` lifecycle-event bookkeeping + `StopAfterStage` — test-only | yagni | -120 | teilweise: Events (≈40 Z.) Kandidat; StopAfterStage ist Fehlerinjektions-Naht → behalten | — | — |
| G08 | `ForwardSystem` / `DeferredSystem` — pimpl around one bool | delete | -130 | bestätigt; Benchmark-Manifest engine_compile_iteration_renderer_surface nutzt Datei → Task inkl. Manifest | — | — |
| G09 | `RenderCommandRouter` → map held in Renderer | shrink | -110 | teilweise: Statustypen bleiben, Gewinn klein → behalten | — | — |
| G10 | Dead public methods (SelectionSystem, ShadowSystem, CullingSystem → PK03, LightSystem, GpuWorld, ColormapSystem, AlwaysOnTop/DepthTested pipeline getters, BindlessHeap EnqueueRawUpdate/SetDefault/GetLayout) | delete | -150 | teilweise: 7 Methoden ohne Aufrufer → Task; Pipeline-Getter + GetLayout widerlegt | — | — |
| G11 | `ICommandContext::BindFrameSampledTexture` — 0 callers | delete | -25 | bestätigt → Task, gebündelt mit G12 (vtable, frisches Build) | — | — |
| G12 | `RHI::IDevice::GetPresentMode` — 0 callers | delete | -15 | bestätigt → mit G11 | — | — |
| G13 | `TextureManager::Reupload` — 0 external callers (confirm) | delete | -40 | bestätigt, aber ≈15 Z. + dokumentierte Streaming-Naht → vertagen | — | — |
| G14 | `CompiledPassDeclarations::Declares*/Require*` — 1 legacy test | delete | -60 | bestätigt (produktiv 0); einzige Naht für Executor-Vertragstest → Operatorentscheid | — | — |
| G15 | 9 private `AlignUp`/`CeilDiv` copies → one shared constexpr helper | reuse | -45 | teilweise: 6× CeilDiv gleich → Task; 4× AlignUp mit abweichender 0-Semantik → separat | — | — |
| G16 | `NextPow2` bit-smear → `std::bit_ceil` | stdlib | -12 | teilweise: saturiert bei Überlauf, Gewinn <10 → behalten | — | — |
| G17 | Vulkan `FallbackBindlessHeap` / `FallbackTransferQueue` → reuse Null backend classes (keep fail-closed) | yagni | -80 | widerlegt (Fable): Fail-closed-Zähler im Vertragstest; Backend-Querkopplung | — | — |
| G18 | `NullProfiler` 400-line state machine (low confidence; depends on Test.Profiler contract) | shrink | -250 | widerlegt (Fable): Referenz für IProfiler-Vertrag (Test.Profiler) | — | — |

### R — Runtime ohne Editor/Modules
| ID | Fund | Tag | Schätz. | Claude | Codex | Operator |
|----|---------|-----|------|--------|--------|----------|
| R01 | `RenderArtifactRegistry` — no production producer; editor reads an always-empty registry | delete | -1100 | teilweise (Fable): kein Produzent, aber GRAPHICS-109/RUNTIME-281 planen einen → vertagen | — | — |
| R02 | Copy-pasted Validate/Get/Set/MakeRegistration for 5 feature config sections (+19 shorter) → one template | shrink | -300 | teilweise: nur Helfer in 3 Point-Dateien heben (≈−60…−110) → Task | — | — |
| R03 | Unused `DebugNameFor*` in GeometryIntegration/AssetWorkflow → see X04 | delete | (X04) | → X04 | — | — |
| R04 | `CameraControllerSlot` Preview/TopDown/EditorSecondary + registry/seed plumbing — test-only | yagni | -60 | widerlegt: RUNTIME-081 verlangt die Slots | — | — |
| R05 | `JobTarget` / `JobDesc::Target` (GpuQueue rejected) | delete | -15 | bestätigt → Task inkl. ADR-0024-Korrektur | — | — |
| R06 | Type-name helpers → see X02 | reuse | (X02) | → X02 | — | — |
| R07 | Dead EngineConfigControl / RenderRecipeActivation API (`LoadAndApply*File`, `ActivateRenderRecipeConfigDocument`, …) | delete | -60 | HotSubsetFile widerlegt (RUNTIME-282 nutzt); RenderRecipeConfigFile-Member vertagen; ActivateDocument behalten | — | — |
| R08 | `SceneDocumentModule::SaveSceneToPath` / `LoadSceneFromPath` — test-only | delete | -65 | teilweise: Test-Rückgrat → vertagen bis Test-Pump-Helfer existiert | — | — |
| R09 | Single-valued `RuntimeInputActionTrigger` | yagni | -15 | teilweise → behalten | — | — |
| R10 | No-bindings overloads of `Build{Mesh,Graph,PointCloud}GeometryPlan` | delete | -45 | bestätigt → Task (mit R13) | — | — |
| R11 | Module OnRegister/OnShutdown provide/withdraw boilerplate ×6 → `ProvideBorrowed<T>` | reuse | -80 | widerlegt: ≈−20 statt −80 | — | — |
| R12 | Test-only/dead helpers (`ScheduleVisualizationHtexRecreate`, `AsPacketBatch`, `AcknowledgeRenderableAssetRebind`, `FindGeometryPropertyCatalogEntry`, `MakeTightLayout`, `ResolveSelected`, `AdvanceWorldGeneration`, `PeekPendingPick`, `OldestInFlightSequence`) | delete | -140 | gemischt: AsPacketBatch → Task; HtexRecreate vertagen; Rest behalten. ⚠ AdvanceWorldGeneration und AcknowledgeRenderableAssetRebind haben keinen produktiven Aufrufer → Verdrahtungslücke klären | — | — |
| R13 | Byte-identical `GraphVertex` / `PointCloudVertex` / `MeshPrimitiveVertex` | reuse | -20 | bestätigt → Task (mit R10) | — | — |
| R14 | `JobService::CancelAll` unused while AsyncWorkModule hand-rolls it | delete/reuse | -23 | bestätigt → Task (AsyncWorkModule ruft CancelAll) | — | — |
| R15 | Small forwarders: `RuntimeAssetIngestDiagnosticFromRouteStatus`, duplicate `ResolveExternalPath`, `ParentPathOf` | delete/shrink | -28 | bestätigt → Task (gebündelt) | — | — |
| R16 | Dead gizmo accessors (`AxisLock`, `SetAxisLock`, `DragAxis`, `DragOrigin`, `MultiSelectPivot`) | delete | -10 | teilweise: Achsensperre-Feature tot (m_AxisLock immer None) → Entscheidung vertagen | — | — |
| R17 | Dead/test-only members (`GetFallback*AttemptCount`, `HasPlotContext`, `NegotiatedVersion`, `YawRadians`, …) | delete | -50 | widerlegt: Fail-closed-Zähler + Testnutzen | — | — |
| R18 | Default `ICameraController::Clone()` returning nullptr — all impls override | shrink | -6 | bestätigt, 0 Zeilen Gewinn → vertagen | — | — |
| R19 | Visualization `IsFinite` overloads → see X03 | stdlib | (X03) | → X03 | — | — |

### E — Runtime-Editor/Modules und Sandbox-App
| ID | Fund | Tag | Schätz. | Claude | Codex | Operator |
|----|---------|-----|------|--------|--------|----------|
| E01 | 9 identical `Prepare*Frame` bodies in `*Operations.Frame.cpp` → one template | shrink | -170 | teilweise: 8 (nicht 9), ≈−40…−70, Source-Guards → vertagen | — | — |
| E02 | Enum-to-string switches → see X04 | shrink | (X04) | → X04 | — | — |
| E03 | 7 pasted result-header blocks in `Sandbox.MeshProcessingPanels.cpp` → `DrawProcessingResultHeader` | shrink | -80 | teilweise (≈−20) → vertagen, Bündel E03/E07/E08 | — | — |
| E04 | `ClusteringService` / `PointCloudConsolidationService` / `TextureBakeService` same forwarding facade | yagni | -90 | widerlegt: tragende Service-Schnittstelle mit Agent-Nutzern | — | — |
| E05 | FNV loops → see X01 | reuse | (X01) | → X01 | — | — |
| E06 | Hand-formatted JSON frame-pacing report in `app/Sandbox/main.cpp` → nlohmann::json | native | -60 | teilweise: ≈−35, neue Link-Abhängigkeit → vertagen | — | — |
| E07 | Three enum-combo idioms + 24 raw ImGui::Combo blocks → `DrawEnumCombo<E>` | shrink | -70 | teilweise (≈−35, Nutzen Typsicherheit) → vertagen, Bündel | — | — |
| E08 | GPU-start lambda pasted 3× in scalar-field panels | shrink | -40 | teilweise (≈−20) → vertagen, Bündel | — | — |
| E09 | EditorShell → EditorUiHost → WindowRegistry delegate-only wrappers | yagni | -60 | teilweise: >100 Aufrufstellen → behalten | — | — |
| E10 | `EditorUiHostOwnerControl` capability token, 1 production caller (contract test greps it) | yagni | -50 | widerlegt: Claim-once-Zugriffsgrenze, Tests + Source-Guard | — | — |
| E11 | Write-only `PhysicsModuleDiagnostics` fields | delete | -30 | bestätigt: 17/37 Felder ungelesen → vertagen bis Diagnostics-Konsument entschieden | — | — |
| E12 | Declaration-only fields (`HasPrimitiveViewEntity`, `PrimitiveViewStableId`, `DefaultEncoder`, `InertiaSum`, `PanelFrameModelBuildTimeNs`) | delete | -5 | bestätigt → Task (mit E13-01) | — | — |
| E13 | 0-caller exports (`ApplyEditorConfiguredPointSampling`, CommandHistory/RenderRecipe DebugNames) | delete | -85 | ApplyEditorConfiguredPointSampling → Task; DebugNames → X04 | — | — |
| E14 | Test-only DebugName exports → see X04 | delete | (X04) | → X04 | — | — |
| E15 | 3 identical point input-catalog wrappers → `GetEditorPointInputCatalog` | yagni | -30 | teilweise: Mindestzahl bewusst verschieden → behalten | — | — |
| E16 | `SandboxEditorController` pimpl with one production caller | yagni | -50 | widerlegt: Kompositionswurzel | — | — |
| E17 | tolower-search lambda in `EditorJobDomainOfBackend` | shrink | -6 | widerlegt: kein Helfer vorhanden | — | — |

### GE — Geometry
| ID | Fund | Tag | Schätz. | Claude | Codex | Operator |
|----|---------|-----|------|--------|--------|----------|
| GE01 | 11 mesh/point-cloud file writers + write-status enums — test-only (planned by UI-046 / RUNTIME-282/283) | delete | -1600 | widerlegt (Fable): 13 Writer ≈1.900 Z.; UI-046 baut ausdrücklich darauf → einordnen UI-046 | — | — |
| GE02 | `Geometry.Graph.Utils` layouts, crossings, BuildKNNGraph, closest-edge queries, edge-length fills — test-only | delete | -1100 | teilweise: Layouts + CountEdgeCrossings (≈700) → Task; ClosestEdge*/EdgeLengths vertagen (GEOM-074); KNN behalten; ApplyGaussianNoise → Testhelfer | — | — |
| GE03 | `Geometry.VectorHeatMethod` — test-only (GEOM-089, METHOD-048) | delete | -820 | widerlegt: GEOM-089 + METHOD-048 planen Nutzung → behalten | — | — |
| GE04 | `Geometry.ConvexHullBuilder` (keep `ConvexHull` type) — test-only | delete | -811 | bestätigt (verwaist) → Task „Anbinden vs. Löschen“ nach PK10 (LocalConvexHull) | — | — |
| GE05 | `Geometry.ImplicitPlaneField` + Octree node properties — test-only | delete | -830 | bestätigt: 791 Z. + Octree-NodeProperties → Task (inkl. 12 tote Test-Imports) | — | — |
| GE06 | `Geometry.RotationAveraging` — test-only | delete | -735 | bestätigt → Task (zieht GE24/GE25-Anteile mit) | — | — |
| GE07 | `Geometry.HtexPatch` — test-only (METHOD-048?) | delete | -510 | teilweise: Teil des entworfenen Htex-Systems (docs/architecture) → vertagen, Operatorentscheid Htex-Richtung | — | — |
| GE08 | `Geometry.HalfedgeMesh.Analysis` — test-only | delete | -511 | widerlegt: GEOM-110/RUNTIME-286/RUNTIME-280 nennen Analyze als Reuse → behalten | — | — |
| GE09 | `Geometry.Graph.ShortestPath` — test-only | delete | -436 | teilweise: Editor-Platzhalter ShortestPath, GEOM-074 → vertagen | — | — |
| GE10 | `Geometry.DomainViews` — test-only | delete | -394 | teilweise: GEOM-012-Ergebnis, METHOD-003 → vertagen | — | — |
| GE11 | Boolean, PointCloud.Conversion, Sphere.Sampling, Curve, HalfedgeMesh.Boundary, Geometry.IO (export tables duplicate Asset.ImportRouter) — test-only | delete | -1370 | zerlegt: Boolean widerlegt (METHOD-005 verbietet Löschen); Boundary widerlegt (GEOM-110); Geometry.IO widerlegt (UI-046, gewollte Doppelableitung aus .inc); PointCloud.Conversion, Sphere.Sampling, Curve (≈743) → Task | — | — |
| GE12 | `Geometry.SDF` + `SDFContact`; Containment imports SDF without using it | delete | -580 | toter `import Geometry.SDF` in Containment → Task; SDF/SDFContact widerlegt (METHOD-003/GEOM-013) | — | — |
| GE13 | `SparseBiCGSTAB`, `SparsePreconditioner`, `AnalyzeSparseMatrix` — test-only | delete | -380 | widerlegt: BiCGSTAB ist METHOD-003-Solverpfad; AnalyzeSparseMatrix produktiv (Eigensolver) | — | — |
| GE14 | `SparseGrid` — test-only | delete | -270 | bestätigt → Task klein oder vertagen bis METHOD-003 | — | — |
| GE15 | Linalg `ComputeQR`, `SolveLeastSquares`, `RobustPCA`, map/ToEigen helpers — test-only | delete | -270 | RobustPCA + ToEigen → Task; QR/LeastSquares vertagen (GEOM-013 QEF) | — | — |
| GE16 | `IsFinite` copies → see X03 | reuse | (X03) | → X03 | — | — |
| GE17 | PLY header/scalar/colour machinery duplicated mesh vs point-cloud readers → `Geometry.IOText.hpp` | reuse | -200 | teilweise: großteils schon in Geometry.IOText.hpp (ef3202f03); Rest ≈60–100 → klein oder behalten | — | — |
| GE18 | Octree `SplitPoint::Median` / `ComputeMedianCenter` — test-only | delete | -80 | bestätigt (niedrige Prio) → Task oder behalten | — | — |
| GE19 | PointSampling mirror enums/struct duplicating ProgressivePoisson types | yagni | -60 | teilweise: Spiegel schützt Modulgrenze zur Referenz-Impl → behalten | — | — |
| GE20 | `IsDelaunay`, `DelaunayFlip`, `CalculateNormals`, `GenerateUVs`, `TargetValence`, `KeepLargestComponent` — 0 callers | delete | -80 | Delaunay + CalculateNormals/GenerateUVs → Task; TargetValence intern genutzt; KeepLargestComponent → einordnen GEOM-110 | — | — |
| GE21 | Vertex attribute-transfer rule hooks — test-only | delete | -70 | bestätigt (nur Test), Hook im Kern verdrahtet → behalten oder kleiner Task | — | — |
| GE22 | Statistics `Skewness`, `Kurtosis`, variances, `SafeAsin` — test-only | delete | -50 | bestätigt → kleiner Task oder behalten | — | — |
| GE23 | `PointCloudIO::LoadPTS`, public NN-histogram/periodogram wrappers | delete | -50 | LoadPTS: PTS wird auf LoadXYZ geroutet → Task (Routing-Entscheid); QualityMetrics widerlegt (GEOM-087) | — | — |
| GE24 | `RandomRotation`, `ChordalDistance`, `ApproxEqual` — 0 production callers | delete | -40 | RandomRotation/ChordalDistance teilweise (Test-Orakel) → behalten; ApproxEqual 0 Aufrufer → Task | — | — |
| GE25 | File-local `kPi` ×4 → `std::numbers::pi` | stdlib | -10 | bestätigt → trivial, mit GE06 bündeln | — | — |

### PK — Codex-Audit-Gruppen ohne Gegenstück im Claude-Audit
| ID | Fund | Tag | Schätz. | Claude | Codex | Operator |
|----|---------|-----|------|--------|-------|----------|
| PK03 | Legacy `CullingSystem` registration API (`Register`, `Unregister`, `UpdateBounds`, `SetDrawTemplate`, `CullingHandle`, `CullSlot`, dead getters) and, as a separate question, the empty `SyncGpuBuffer()` still called from `RenderPrepPipeline.cpp:133` | delete | small | bestätigt (Fable): Legacy-API + Getter → Task; leere Methode entfernen, Schritt CullingSync behalten (Vertrag; Schritt-Entfernung vertagen bis GRAPHICS-135) | — | — |
| PK04 | Duplicate `MakeDenseClosedTriangleMesh` / `ExtractTriangleSoup` in `Test.MeshOperationsSlow.cpp` vs `Test_MeshOperations.cpp` → `tests/support/geometry/Test_MeshBuilders.h` | reuse | -85 | bestätigt: 93 identische Zeilen → Task (≈−85) | — | — |
| PK05 | Unused types `CompiledGraph`, `ReadyNodePolicy`, `VulkanQueueFamilies`/`VulkanQueues`, `VulkanSurfaceState` in 4 partitions | delete | -80 | bestätigt: 4 tote Partitionen → 2 Tasks (core, vulkan) | — | — |
| PK07 | GLFW `CreateVulkanSurface` bridge (0 callers; Vulkan.Device builds the surface itself) | delete | -54 | bestätigt: 0 Aufrufer → Task; Operator klärt Layer-Richtung, PLATFORM-004 anpassen | — | — |
| PK08 | Duplicate DAG cycle search in `Core.Dag.Scheduler.cpp` and `Core.Dag.TaskGraph.cpp` | reuse | -50 | teilweise: ≈−35, Owner unklar → behalten | — | — |
| PK09 | Duplicate LOP benchmark metric helpers (`MeanPlaneError`, `MeanSphereError`, `MinimumPairwiseDistance`, `Finite`) | reuse | -40 | bestätigt → Task (gemeinsamer Benchmark-Header, EAR optional) | — | — |
| PK10 | `CachedSelectedVertex/Edge/FaceIndices` (declaration only) and `Culling::Proxy` / `CullableTag` (0 importers) — four separate candidates | delete | -20 | bestätigt: 3 Caches + CullableTag + Proxy → Task (ECS-Cleanup) | — | — |
| PK11 | `Core::PathKey` / `FromPath` (0 users; own FNV copy, see X01) | delete | -25 | bestätigt → Task (entfernen, kein Hash-Reuse) | — | — |
| PK12 | Legacy `propertyName` / `expectedValueKind` read fallbacks in `Runtime.SceneSerialization.cpp:1276-1298` (writer emits `name`/`valueKind`) | delete | -20 | bestätigt: Loader akzeptiert nur v4 → Legacy-Schlüssel unerreichbar → Task | — | — |

## Claude-Gegenprüfung 2026-10-03

Erste Claude-Prüfrunde auf `d12288094` (rein lesend, keine Builds). Das Votum
steht verkürzt in der Spalte „Claude“; Belege, Gegenbelege und Prüfbefehle
lagen in den Agentenberichten der Sitzung und werden beim jeweiligen
Kandidaten-Dossier in Phase B ausgeschrieben. Die Codex-Spalte und die
Operator-Spalte sind bewusst offen.

- Prüfer: Fable 5.1 für Urteilsfragen (E0 PK, E1 X, E7 GE sowie G06, G17,
  G18, R01, C01, C02, T12); Sonnet für die grep-lastigen Etappen (E2 T, E3 C,
  E4 G, E5/E6 R/E).
- Ergebnis grob: Viele große Einsparungen des Claude-Audits sind widerlegt
  oder durch offene Tasks gedeckt (z. B. GE01/GE03/GE08/GE13, G02, T12/T13,
  C10/C14). Die realistische freie Löschmenge liegt eher bei einigen tausend
  statt ≈38.000 Zeilen. Die Codex-Gruppen PK03–PK12 sind fast vollständig
  bestätigt.
- Neue Überschneidung: `LEGACY-043` löscht bereits einen Teil der Shader aus
  G01/G03 (903 Zeilen) → dort einordnen, nicht doppelt zählen.
- Auffälligkeiten außerhalb von Ponytail, separat zu klären (keine
  Löschkandidaten):
  - `JobService::AdvanceWorldGeneration` hat keinen produktiven Aufrufer,
    während fünf produktive Stellen `WorldGeneration` als Schutz lesen
    (mögliche fehlende Verdrahtung beim Szenenwechsel).
  - `AcknowledgeRenderableAssetRebind` ist laut ADR 0013 Pflichtschritt der
    Rebind-Schleife, hat aber keinen produktiven Aufrufer.
  - `HashProceduralGeometryParams` hasht rohe Struct-Bytes inklusive Padding
    (möglicherweise nichtdeterministisch).
  - Gizmo-Achsensperre ist funktional tot (`m_AxisLock` immer `None`).
  - Editor-Algorithmen `ShortestPath`, `ConvexHull`, `VectorHeat`,
    `BooleanCSG` haben Label und UI-Reihenfolge, aber keinen Ausführungspfad.
  - Die lokalen Worktrees unter `.claude/worktrees/` enthalten 4 Commits,
    die nicht auf `main` liegen (keine Repo-Änderung; Operator entscheidet).

## Prüfhinweise aus dem Codex-Audit

Die folgenden Hinweise stammen aus den zwölf Ausgangsgruppen des Codex-Audits
und gelten für die angegebenen Inventarzeilen.

### PK01 → G01 — 20 möglicherweise überholte Shader

- Ausgangshypothese: etwa 1.630 Dateizeilen ohne aktuellen Verbraucher;
  [CompileShaders.cmake](../../../cmake/CompileShaders.cmake) kompiliert sie
  weiterhin über den rekursiven Glob.
- Vollständige Prüfliste: `point_surfel.vert`, `point_surfel.frag`,
  `point_retained.vert`, `point_retained.frag`, `point_flatdisc.vert`,
  `point_flatdisc.frag`, `point_sphere.vert`, `point_sphere.frag`,
  `pick_mesh.vert`, `pick_mesh.frag`, `pick_line.vert`, `pick_line.frag`,
  `pick_point.vert`, `pick_point.frag`, `pick_id.vert`, `pick_id.frag`,
  `scene_update.comp`, `instance_cull_multigeo.comp`, `debug_view.comp`,
  `deferred/gbuffer.vert`, jeweils unter `assets/shaders/`.
- Einstieg: [point_retained.vert](../../../assets/shaders/point_retained.vert),
  [Renderer](../../../src/graphics/renderer/Graphics.Renderer.cpp),
  [RendererFrameLifecycle-Tests](../../../tests/contract/graphics/Test.RendererFrameLifecycle.cpp).
- Gegenprüfung: dynamisch zusammengesetzte Shadernamen, Pipeline-Varianten,
  benutzerseitig wählbare Pfade, Includes, Fixtures, Build-/Packaging-Nutzung
  und vorhandene GPU-Smokes. Ähnlich benannte aktuelle Shader nicht einbeziehen.
- Für jeden Kandidaten den wirklichen aktuellen Ersatzpfad oder den Nachweis
  nennen, dass die Funktion entfallen ist. Ein Vergleich der Dateinamen reicht
  nicht; Push-Constants, Bindings und unterstützte Primitive gehören dazu.

### PK02 → G05 — Alter GpuScene-Wrapper

- Ausgangshypothese: [Interface](../../../src/graphics/renderer/Graphics.GpuScene.cppm)
  und [Implementierung](../../../src/graphics/renderer/Graphics.GpuScene.cpp)
  enthalten zusammen 499 Dateizeilen; keine Importierer im ersten Scan gefunden.
- Gegenprüfung: aktuelle und bedingte Build-Ziele, Re-Exports, Tests,
  Migrationsaufrufer, Slot-Verantwortung und Buffer-Lifetime.
- Die Behauptung prüfen, dass
  [GpuWorld](../../../src/graphics/renderer/Graphics.GpuWorld.cppm) alle heute
  benötigten Zuständigkeiten trägt. Kommentare über alte Testkompatibilität
  weder als Beweis für Nutzung noch als Beweis für Entbehrlichkeit behandeln.

### PK03 — Alte Culling-Registrierung und leerer Sync-Schritt

- Ausgangshypothese: rund 110 Zeilen für `Register`, `Unregister`,
  `UpdateBounds`, `SetDrawTemplate`, `CullingHandle`, `CullSlot` und zugehörige
  Verwaltung könnten entfallen; `SyncGpuBuffer()` ist leer und wird aufgerufen.
- Einstieg: [CullingSystem](../../../src/graphics/renderer/Graphics.CullingSystem.cpp),
  [Interface](../../../src/graphics/renderer/Graphics.CullingSystem.cppm),
  [RenderPrepPipeline](../../../src/graphics/renderer/Graphics.RenderPrepPipeline.cpp).
- Gegenprüfung: öffentliche Methoden getrennt von gleichnamigen Methoden
  anderer Systeme suchen; Tests, Registrierungsdiagnostik und Lebenszeitregeln
  prüfen. Gemeinsam benutzte Bucket-Kapazität und aktive GPU-Culling-Logik
  dürfen nicht mit alter CPU-Slot-Verwaltung verwechselt werden.
- Gesondert klären, ob das Entfernen des leeren Aufrufs einen beobachtbaren
  Phasen-/Diagnostikvertrag berührt. Mindestens zwei Teilfragen sichtbar halten.

### PK04 — Doppelte Helfer in schnellen und langsamen Mesh-Tests

- Ausgangshypothese: 97 identische Zeilen, netto ungefähr 85 Zeilen einsparbar,
  zwischen [Test.MeshOperationsSlow.cpp](../../../tests/unit/geometry/Test.MeshOperationsSlow.cpp)
  und [Test_MeshOperations.cpp](../../../tests/unit/geometry/Test_MeshOperations.cpp).
- Kandidaten: `MakeDenseClosedTriangleMesh`, `ExtractTriangleSoup` und der
  zugehörige Mesh-Wiederaufbau. Funktionen einzeln auf identische Annahmen prüfen.
- Möglicher Owner: bestehender
  [Mesh-Test-Support](../../../tests/support/geometry/Test_MeshBuilders.h).
- Gegenprüfung: zusätzlicher Modul-/Linkbedarf, Assertion-Verhalten, Zugriff
  beider Testziele und langsame Testlabels. Gemeinsamer Support muss weniger
  Aufwand verursachen als die Dopplung; Tests und ihre Unabhängigkeit erhalten.

### PK05 — Vier Partitionen mit möglicherweise ungenutzten Typen

- Ausgangshypothese: ungefähr 80 Zeilen einschließlich Build-/Importeinträgen.
- Einzeln prüfen:
  [Scheduler.Compiler](../../../src/core/Core.Dag.Scheduler.Compiler.cppm)
  mit `CompiledGraph`,
  [Scheduler.Policy](../../../src/core/Core.Dag.Scheduler.Policy.cppm)
  mit `ReadyNodePolicy`,
  [Vulkan.Queues](../../../src/graphics/vulkan/Backends.Vulkan.Queues.cppm)
  mit `VulkanQueueFamilies` und `VulkanQueues`,
  [Vulkan.Surface](../../../src/graphics/vulkan/Backends.Vulkan.Surface.cppm)
  mit `VulkanSurfaceState`.
- Gegenprüfung: exakte Symbolgrenzen und Namespace beachten, transitive
  Re-Exports und Importabhängigkeiten verfolgen. Der gleichartig benannte
  aktuelle RenderGraph-Compiler gehört nicht automatisch zu diesem Fund.
- Pro Partition klären, ob nur der Typ entfällt oder ob Importkanten auf
  tatsächlich genutzte Definitionen explizit umgestellt werden müssten.

### PK06 → C07 — Core.RingBuffer

- Ausgangshypothese: 66 Zeilen in
  [Core.RingBuffer.cppm](../../../src/core/Core.RingBuffer.cppm) ohne Import
  oder Instanziierung.
- Gegenprüfung: Scheduler-Interna, Logging-Ringpuffer, Tests, Benchmarks und
  bedingte Konfigurationen auseinanderhalten. Ähnliche Namen sind kein
  Nutzungsnachweis für genau dieses Template.
- Entscheidung betrifft nur das konkrete Modul und zugehörige aktuelle
  Build-/Dokumentationseinträge, nicht Ringpuffer als grundsätzliches Konzept.

### PK07 — GLFW-Vulkan-Surface-Brücke

- Ausgangshypothese: 54 Zeilen in
  [Interface](../../../src/platform/backends/glfw/Platform.Backend.GlfwVulkanSurface.cppm)
  und [Implementierung](../../../src/platform/backends/glfw/Platform.Backend.GlfwVulkanSurface.cpp)
  ohne Aufruf von `CreateVulkanSurface`.
- Gegenprüfung: alle GLFW-/Vulkan-Buildpfade und den direkten Surface-Aufbau
  in [Vulkan.Device](../../../src/graphics/vulkan/Backends.Vulkan.Device.cpp)
  vergleichen. Das Vorhandensein zweier ähnlicher Funktionen allein entscheidet
  nicht über deren Layer-Verantwortung.
- Eine mögliche Entfernung darf keine neue Plattform-/Grafik-Abhängigkeit
  einführen. Notwendige bestehende RHI-/Plattformgrenzen ausdrücklich benennen.

### PK08 — Doppelte DAG-Zyklensuche

- Ausgangshypothese: ungefähr 50 Zeilen netto durch gemeinsame interne Logik
  zwischen [Scheduler](../../../src/core/Core.Dag.Scheduler.cpp) und
  [TaskGraph](../../../src/core/Core.Dag.TaskGraph.cpp) einsparbar.
- Gegenprüfung: Knotentypen, Traversierungsreihenfolge, Begrenzung des
  Diagnosepfads, Selbstzyklen, Fehlerdarstellung und deterministisches Ergebnis.
- Als Alternative „Dopplung behalten“ bewerten, falls eine gemeinsame Funktion
  zusätzliche Templates, Abhängigkeiten oder unklare Zuständigkeiten erzeugt.
  Nicht den gesamten Scheduler oder TaskGraph zusammenführen.

### PK09 — Doppelte LOP-Benchmark-Messhelfer

- Ausgangshypothese: ungefähr 40 Zeilen netto bei `MeanPlaneError`,
  `MeanSphereError`, `MinimumPairwiseDistance` und `Finite`.
- Einstieg: [Continuous LOP](../../../benchmarks/geometry/Bench_ContinuousLopReferenceSmoke.cpp)
  und [LOP/WLOP](../../../benchmarks/geometry/Bench_PointCloudConsolidationReferenceSmoke.cpp).
- Gegenprüfung: Leerfälle, NaN/Inf-Verhalten, Präzision, Maßeinheiten und
  tatsächlich identische Semantik. Einen geeigneten bestehenden Benchmark-
  Support-Owner suchen, ohne Benchmarks an interne Methodenimplementierungen
  oder Test-Only-Code zu koppeln.
- Unterschiedliche Bestätigungsdaten, Seeds, Parameter und unabhängige
  Referenznachweise nicht vereinheitlichen, nur weil Messhelfer ähnlich sind.
  Keine Performanceaussage oder Neubesiegelung historischer Resultate ableiten.

### PK10 — Selection-Caches und Culling-Proxy in ECS

- Ausgangshypothese: ungefähr 30 Zeilen für `CachedSelectedVertexIndices`,
  `CachedSelectedEdgeIndices`, `CachedSelectedFaceIndices`, `Culling::Proxy`
  und `CullableTag` ohne aktuelle Verbraucher.
- Einstieg: [Selection](../../../src/ecs/Components/ECS.Component.Selection.cppm)
  und [Culling.Proxy](../../../src/ecs/Components/ECS.Component.Culling.Proxy.cppm).
- Gegenprüfung: Komponenten-Registrierung, entt-Verwendung, Serialize/Load,
  Picking-/Selection-Pfade, Tests und mögliche deklarierte aktive Owner.
  Jeder Cache-Typ und der Proxy bekommen eine getrennte Entscheidung.
- Verwendete Selection-Tags und der aktuelle Stable-ID-/Picking-Vertrag
  gehören nicht zur pauschalen Löschhypothese.

### PK11 — PathKey und seine Hashimplementierung

- Ausgangshypothese: ungefähr 25 Zeilen für `PathKey` und `FromPath` in
  [IOBackend-Interface](../../../src/core/Core.IOBackend.cppm) und
  [Implementierung](../../../src/core/Core.IOBackend.cpp) ohne Verbraucher.
- Gegenprüfung: Asset-I/O, Cache-Identitäten, Tests, Konfiguration und aktive
  Aufgaben. Ungenutzten Typ entfernen und Hash-Reuse sind unterschiedliche
  Optionen, die nicht beide ohne Bedarf umgesetzt werden sollen.
- Falls doch ein Verbraucher gefunden wird, dessen Semantik vor einem Wechsel
  zu [Core.Hash](../../../src/core/Core.Hash.cppm) prüfen: Breite, Bytebehandlung,
  Nullzeichen, Nullwert und Persistenzbedeutung der Identität.

### PK12 — Alte Schreibweisen beim Lesen von Property-Referenzen

- Ausgangshypothese: ungefähr 20 Zeilen im
  [Scene-Reader](../../../src/runtime/Scene/Runtime.SceneSerialization.cpp)
  für `propertyName` und `expectedValueKind`, während der Writer `name` und
  `valueKind` schreibt.
- Gegenprüfung: alle heutigen Writer/Reader, Szenen und Fixtures sowie
  [SceneSerialization-Tests](../../../tests/contract/runtime/Test.RuntimeSceneSerialization.cpp).
  Insbesondere die Semantik von `Any`/`Unknown` und unconstrained bindings
  getrennt von einer reinen Feldumbenennung verstehen.
- Der fehlende allgemeine Kompatibilitätsanspruch im Repository ist ein
  Ausgangspunkt für die Entscheidung, kein Ersatz für die Prüfung aktueller
  Save/Load-Roundtrips und nutzbarer Szenen. Der Operator entscheidet über den
  konkreten Umfang und mögliche Auswirkungen auf seine Dateien.

## Dossier je Prüfkandidat

Die Dossiers werden während der Bearbeitung unterhalb des Inventars
in dieser Task-Datei ergänzt. Keine neue Datenbank, kein eigenes Review-Tool
und keine automatische Task-Erzeugung sind erforderlich.

Jedes Dossier enthält:

1. **Kennung, Herkunft, Entscheidungssatz:** genaue Frage und Bezug zur Inventarzeile.
2. **Prüfstand:** Datum, Revision, sauberer/veränderter Arbeitsbaum und relevante
   Quellen mit Zeilen oder Symbolnamen. Bei Drift betroffene Voten neu prüfen.
3. **Heutige Aufgabe:** Owner/Layer, Verbraucher, Build-Ziele und tatsächlich
   unterstützte Benutzerfunktionen oder technische Verträge.
4. **Evidenz:** relevante Suchbefehle und Ergebnisse, dynamische Referenzen,
   Tests/Fixtures, aktive Aufgaben und Grenzen des Nachweises.
5. **Optionen:** behalten, kleinste Vereinfachung und gegebenenfalls andere
   Zuständigkeit. Nötigen Ersatz und zusätzliche Komplexität ausdrücklich zählen.
6. **Risiken und Prüfplan:** Verhaltens-, API-, Format-, Lifetime-, Backend-
   und Buildauswirkungen; die konkreten betroffenen Tests und ausführbaren
   fokussierten Befehle für einen späteren Umsetzungstask bestimmen.
7. **Votum des Bearbeiters:** Empfehlung mit Begründung und Unsicherheiten.
8. **Votum des Gegenprüfers:** tatsächliches separates Review mit Quellen, Gegenbelegen
   und betrachtetem Stand; offene Punkte nicht als Zustimmung zusammenfassen.
9. **Gemeinsame Besprechung:** Operator-Fragen und Antworten, verbleibende
   Unterschiede zwischen den Voten und noch fehlende Nachweise.
10. **Operator-Entscheidung:** Datum, bestätigter Inhalt, Bedingungen und
    gegebenenfalls Link auf den ausdrücklich freigegebenen Folgetask.

## Zulässige Entscheidungen

- **Task erstellen:** eine konkrete kleine Umsetzung wird als Backlog-Task
  festgehalten; die separate Ausführungsfreigabe bleibt sichtbar.
- **Behalten:** der bestehende Code hat einen benannten Nutzen oder die
  vorgeschlagene Änderung lohnt sich nicht. Begründung als spätere Orientierung.
- **Fund widerlegt:** die Audit-Prämisse war falsch; Gegenbeleg festhalten und
  die ursprüngliche Einsparschätzung entsprechend korrigieren.
- **Vertagen:** offene Frage, fehlender Nachweis oder bewusst niedrige Priorität;
  Grund und gegebenenfalls konkreten Wiedervorlageanlass festhalten.
- **Weiter zerlegen:** Entscheidung noch nicht abgeschlossen; kleinere
  Kandidaten ableiten und einzeln wieder in den Review-Ablauf nehmen.

## Acceptance criteria

- [ ] Alle Inventarzeilen beider Audits sind vollständig auf kleine
  Prüfkandidaten abgebildet; kein ursprünglicher Fund verschwindet ohne
  Erklärung.
- [ ] Jeder endgültig entschiedene Kandidat besitzt ein vollständiges Dossier
  mit getrennten, echten Claude- und Codex-Einschätzungen sowie der Beteiligung
  des Operators; Abweichungen sind ausschließlich auf dessen ausdrücklichen
  Wunsch dokumentiert.
- [ ] Jede finale Entscheidung stammt ausdrücklich vom Operator und bezieht
  sich auf den dokumentierten Kandidatenumfang und Prüfstand.
- [ ] Alle Entscheidungen sind als Task erstellen, behalten, widerlegt oder
  ausdrücklich vertagt abgeschlossen; weiter zu zerlegende Fragen bleiben offen.
- [ ] Nur vom Operator ausgewählte Kandidaten wurden zu konkreten Folgetasks;
  jedes neue Task-Dokument ist mit seiner Entscheidung verknüpft, besitzt
  eigene Contract- und Verifikationsangaben, und keine Änderung hat zwei
  Umsetzungstasks.
- [ ] Erhaltensgründe, Widerlegungen und Vertagungen bleiben nachvollziehbar;
  Einsparungen werden weder mehrfach gezählt noch als bereits erreicht gemeldet.
- [ ] Im Rahmen dieses übergeordneten Tasks wurden keine Engine-, Shader-,
  Build-, Format-, Tooling- oder Teständerungen vorgenommen und keine Umsetzung
  gestartet.
- [ ] Task-Metadaten, Links und Session-Brief bestehen die strukturellen Checks.

## Verification

Für Anlage und Pflege dieser Review-Notiz gelten die Dokumentationsprüfungen:

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
git diff --check
```

Nach Öffnen, Verschieben oder Re-Gating von Tasks vorher den Session-Brief
mit `python3 tools/agents/generate_session_brief.py` aktualisieren.

Quellcodeprüfungen für einen Kandidaten müssen ihren tatsächlichen Umfang
ausweisen. Ein statischer Review beweist keine erfolgreiche Löschung. Falls
für eine Entscheidung Laufzeitevidenz erforderlich ist, zuerst die betroffenen
Tests über `build/ci/test-inventories/RegisteredTestSources.tsv` zuordnen und
die passenden fokussierten Befehle im Dossier festhalten. Ein Build des
unveränderten Bestands beweist keine Funktionsfähigkeit nach einer Entfernung.
Experimentelle Codeänderungen bedürfen einer gesonderten Operator-Anweisung.

Die CPU-, Vulkan- und Sanitizer-Nachweise eines späteren Umsetzungstasks bleiben
getrennt. Dieser Review-Task fordert keinen pauschalen Vollbuild. Die
Verifikationsregeln aus `AGENTS.md` gelten für jeden späteren Umfang unverändert.

## Abschluss und Fortsetzung

Dieser Task ist abgeschlossen, sobald die zerlegten Kandidaten geprüft und
vom Operator entschieden sind und die ausdrücklich gewünschten Folgetasks
konkret vorliegen. Die Implementierung dieser Folgetasks ist kein
Abschlusskriterium des übergeordneten Reviews. Auch eine begründete Entscheidung,
keinen einzigen Umsetzungstask anzulegen, erfüllt sein Ziel.

Bei Unterbrechung die zuletzt abgeschlossene Entscheidung, den nächsten
Kandidaten und dessen fehlenden Nachweis notieren. Nach Wiederaufnahme keine
schon getroffene Entscheidung erneut einfordern, solange Umfang und relevante
Evidenz unverändert sind. Änderungen am geprüften Code erfordern die erneute
Prüfung der betroffenen Annahmen, nicht den Neustart der gesamten Liste.

## Log

- 2026-10-03: Aus REVIEW-005 (Claude-Audit, 123 Inventarzeilen) und
  REVIEW-006 (Codex-Audit, PK01–PK12) auf Operator-Wunsch zusammengeführt.
  Überschneidungen vereint: G01+PK01, G05+PK02, C07+PK06; der Culling-Teil von
  G10 liegt bei PK03, der Culling-Proxy aus C15 bei PK10. G01 enthält
  zusätzlich acht ungenutzte Root-Shader (`line`, `point`, `triangle`,
  `debug_surface`), die das Codex-Audit nicht nannte. Rollen verallgemeinert:
  der sitzungsführende Agent ist Bearbeiter, der andere Gegenprüfer. Keine
  Untertasks angelegt, kein Kandidatenreview gestartet, keine Entscheidung
  über eine Codeänderung getroffen. REVIEW-005 und REVIEW-006 als ersetzte
  Planungsnotizen retired. Nächster Schritt bei Aufnahme: Phase A,
  gemeinsame Zerlegung und Festlegung der ersten Einzelprüfung.
- 2026-10-03: Erste Claude-Gegenprüfung aller 132 Inventarzeilen auf
  `d12288094` (Fable 5.1 für Urteilsfragen, Sonnet für grep-lastige Etappen;
  rein lesend). Voten in der Claude-Spalte, Zusammenfassung und Nebenbefunde
  in §„Claude-Gegenprüfung 2026-10-03“. Codex- und Operator-Spalten offen;
  keine Entscheidung, kein Folgetask, keine Codeänderung.
