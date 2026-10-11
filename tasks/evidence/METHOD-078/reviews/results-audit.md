# Independent results audit — 2026-10-11

Reviewer `/root/nystrom_results_review`, requested gpt-6-astra/xhigh. Read-only/no-delegation instruction-only. The reviewer did not read the author's audit before its verdict. Actual answer:

**PASS für die Ergebnisauswertung; Kandidat nicht übernehmen.** Die Akzeptanzkriterien 1 und 2 von METHOD-078 sind erfüllt. Kriterium 3 bleibt bis zur geprüften Bereinigung offen.

Bindung: Commit `0efb7e48f87ee57fd5cb34f2c5d072fb71391d3e`, Preflight-Digest `e75cf4c61d104b0529f5f1ba1e24a0e8eed6728d1c55053e6ef396aae2f2f229`.

| Prüfung | Points/fixed | Box/resampled |
|---|---:|---:|
| Akzeptierte Nyström-Iterationen, Baseline → Kandidat | 6 → 6 | 1 → 1 |
| Versuche | 9 → 22 | 4 → 40 |
| Baseline-Zeit, beide Wiederholungen | 151,221–151,399 s | 201,525–202,195 s |
| Kandidatenzeit | 159,070–160,567 s | 216,216–216,727 s |
| Langsamster Kandidat / schnellste Baseline | 1,06180 | 1,07543 |
| RMS / Maximum gegen Auto, Weltkoordinaten | 5,079e−7 / 1,297e−6 | 8,098e−5 / 2,124e−4 |

Beide Zeitverhältnisse verfehlen die eingefrorene Grenze `< 0,9`. Alle zusätzlichen Versuche enden mit `invalid_target`; zusätzliche akzeptierte Iterationen fehlen.

Die Ursachen sind unterscheidbar: Points hat numerische Ablehnungen, bei Iteration 6 anschließend den Kostenstopp und danach 13 unterdrückte Iterationen. Box beginnt mit einem Stichprobenfehler von `0,0140272 > 0,001`, akzeptiert nach Vergrößerung einmal, scheitert danach numerisch und erreicht das Kostenlimit; anschließend werden 18 Iterationen unterdrückt. Kein beobachteter zusätzlicher Retry behebt diese Ablehnungen. Dass das Kostenlimit erreicht wird, beweist **nicht**, dass noch größere Landmark-Mengen erfolgreich wären.

Alle elf Zellen, Befehle und Quell-/Binärbindungen stimmen. Auto und Kostenkontrolle benutzen keine Nyström-Iteration und liefern identische Positionen. Die Negativkontrolle erreicht RMS `0,90495855`, überschreitet somit die Ablehnungsgrenze deutlich. Ihre fünf akzeptierten E-Schritte bei vier abgeschlossenen Iterationen sind durch die Konvergenzprüfung vor dem letzten M-Schritt erklärt.

RMS und Maximum wurden unabhängig über sämtliche 100.000 Positionen jeder Zelle nachgerechnet. Wiederholungen stimmen in allen JSON-Feldern außer Zeit und in vollständigen Positionspuffern überein. Auch Baseline und Kandidat liefern je Variante identische Positionspuffer.

Der HTML-Bericht enthält elf Resultate, 22 gültige Abbildungen, elf Rohlog-Verweise und den korrekten eingefrorenen Ablehnungsentscheid. Iterations-, Zielgrößen- und Phasenspuren sind vollständig; exemplarische Abbildungen wurden visuell geprüft. Die frühe Zeit-Rückgabe von `assessed()` überspringt teilweise spätere Wiederholungsprüfungen; meine unabhängige Prüfung deckte alle Wiederholungen ab. Keine ergebnisändernde Diskrepanz gefunden.

Rohdatendigest: `c87f162607b11b3ff966c02263b09a7ea2e6dad637a9dddad2c50cf84546bac8`. Definition: SHA256 der nach Basename sortierten Zeilen `<Datei-SHA256>  <Basename>\n` für `.json`, `.bin`, `.stderr`, `.stdout`, `.command.json`, `.resources.json` aller elf Zellen. Berichtsdigest: `efcbd295e5abcbe347edf0109e3ab2d42156a05fdd0eb102c0b283cfa41083fc`.

`check_ara_claims.py --root . --strict` besteht mit 118 Claims. Die Autoren-Auswertung wurde vor diesem Urteil nicht gelesen.

Keine neuen Experimente oder Builds ausgeführt. Aussagegrenze: CPU-Pilot, ein Seed, zwei Varianten, 20 Iterationen; keine erfolgreiche Registrierung, allgemeine Beschleunigung oder Produktionsfreigabe nachgewiesen.
