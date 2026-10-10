# Preparation code review — 2026-10-11

Codex coordinator → fresh `/root/diagnostic_code_review`, requested gpt-6-astra/xhigh.
Read-only/no-delegation instruction-only. No experiment approval. Final answer verbatim:

**Verdikt: APPROVE für die Diagnosevorbereitung.** Alle gefundenen Fehler sind behoben; keine offenen Blocker im geprüften Umfang. Dies ist keine Experiment- oder Produktionsfreigabe.

Die ursprünglichen Befunde, unverändert:

> **[P1] Abgebrochene Läufe erhalten keinen aktuellen HTML-Bericht.** In diagnose.py:188 führen Timeout oder ein Fehlercode vor `report()` zum Abbruch. Beim ersten Lauf fehlt der Bericht vollständig; später bleibt der vorherige Bericht stehen und verschweigt den fehlgeschlagenen Lauf. Auch ein nachträglicher `report` überspringt Läufe ohne JSON und verlangt vollständige Positionsdateien. Kleinste Korrektur: Abbruchstatus speichern und vorhandene Teil-Traces auch ohne Ergebnis-JSON oder Positionen berichten; Berichtserstellung beim Fehlerpfad sicherstellen. Ein deterministischer Selbsttest muss diesen Fall abdecken.

> **[P2] Erfasste Phasen erscheinen nicht im Bericht.** Der Runner schreibt `CPD_STAGE` mit Zeitstempeln, aber diagnose.py:48 ignoriert sie. Die Plots markieren ausschließlich die erste unterdrückte Iteration; der Rohlog-Link ersetzt die vorgeschriebenen markierten Phasengrenzen nicht. Kleinste Korrektur: Phasen parsen und mit Zeitzuordnung im Bericht darstellen.

**Beide geschlossen:** Fehlerstatus und Teilberichte werden erzeugt; vollständige Trace-Zeilen bleiben bei einem abgeschnittenen letzten Schreibvorgang erhalten; fehlende oder nicht endliche Vergleichswerte werden vor Zahlenvergleichen abgewiesen; Phasen werden zeitlich dargestellt. Die zwischenzeitliche Regression beim Exact-Arm ist ebenfalls geschlossen: Vergleichsmetriken sind ausschließlich für Nicht-Exact-Arme erforderlich, und der Selbsttest bildet das tatsächliche Exact-Ausgabeformat ab.

Endgültige SHA256-Bindung:

- Driver: `447c53506202d9e8eb8051f1ce818b6f72810bd18c573a2dd82717c590c31d1f`
- Ursprünglicher Vorbereitung-Diff: `1ed6d6d879c912d83aa0e10ca402e8fa3b635db0adb72e42cb0f054310683f79`
- Bindings: `4fbd406228794d25503d47ebea3f426df0295a02b1da69f2136c22592f29f0be`

Keine Builds, Tests oder wissenschaftlichen Läufe selbst ausgeführt; Review schreibgeschützt. Ein äußerer Prozessabbruch kann weiterhin die Berichtserstellung unterbrechen; erhaltene Rohdaten benötigen dann eine erneute Berichtserstellung.

Link formatting in the quoted source locations was normalized to plain locations for the repository doc validator; answer wording is unchanged.

## Delta — timeout handling, same reviewer, final answer verbatim

**Verdikt: APPROVE für diesen Vorbereitungscode-Diff.** Keine offenen Korrektheits- oder Komplexitätsbefunde gegenüber `96c21108f2831a86b290db3a6374d3e75ea37fc0`.

- `SIGALRM` und `SIGTERM` gelangen in den gemeinsamen Fehlerpfad; `failure.json` wird vor dem Wiederherstellungsbericht geschrieben.
- Der Fehlerstatus übersteuert die Bewertung auch bei vollständig vorhandenen Ergebniszeilen mit `UNEVALUATED`.
- Der Alarm wird vor der Wiederherstellung und abschließend deaktiviert.
- Die Änderungen bleiben klein und zweckgebunden. Die historische Ergebnisbehauptung wurde entfernt.

Geprüfter Driver-SHA256: `8460d6374f281c6b0506447303b4dd7b9525841cfd536d78958237c3639a01bb`.

Keine Tests selbst ausgeführt; insbesondere Signalzustellung nicht praktisch geprüft. Ein erzwungenes `SIGKILL` bleibt unbehandelbar. Keine Experimentfreigabe.
