# Berichte zu src

Diese Übersicht sammelt die angeforderten Prüfungen der Unterordner von `src`.
Sie ist der Einstieg für die anschließende gemeinsame Auswertung durch Codex 6
Astra Ultra. Jeder Bericht gilt für seinen genannten Quellstand; ein Befund ist
erst nach erneuter Prüfung als noch offen zu behandeln.

Die [gemeinsame Auswertung mit priorisiertem Umsetzungsplan](2026-10-16-src-audit-implementation-plan.md)
führt doppelte Befunde zusammen und ordnet die fachlichen Zuständigkeiten zu.
Der aktuelle Umsetzungs- und Prüfstand wird in
[HARDEN-090](../../tasks/active/HARDEN-090-src-audit-remediation.md) geführt.

| Bereich | Bericht | Geprüfter Commit | Ergebnis |
| --- | --- | --- | --- |
| `src/assets` | [Assets Audit vom 16. Oktober 2026](2026-10-16-src-assets-audit.md) | `66da33fd6` | Drei reproduzierte Vertragslücken; gezielte Bereinigung, flache Struktur behalten |
| `src/graphics` | [Graphics Audit und Übergabe](../reports/2026-10-16-src-graphics-audit.md) | `66da33fd6` | Acht Befunde mit Quellankern und Prüfgrenzen; zwei CPU-Allocator-Repros, zwei arithmetische Proben, 264 grüne Bestandstests; kein GPU-Nachweis |
| `src/ecs` | [ECS Audit und Übergabe](2026-10-16-src-ecs-audit.md) | `66da33fd6` | Sieben reproduzierte Fehlerfälle; 14 Befunde mit Fundstellen, Fix-/Testhinweisen und Runtime-Grenzen |
| `src/core` | [Core Audit und Übergabe](2026-10-16-src-core-audit.md) | `66da33fd6` | Befunde, Reproduktionsrezepte, Evidenzgrenzen und bereichsübergreifende Zuständigkeiten im Bericht |
| `src/geometry` | [Geometry Audit und Übergabe](2026-10-16-src-geometry-audit.md) | `66da33fd6` | 15 Befunde und Empfehlungen; 13 fehlschlagende Repro-Assertions, 205 grüne vorhandene Tests; Quellen, Owner und Prüfgrenzen getrennt |
| `src/physics` | [Physics Audit und Übergabe](../reports/2026-10-16-src-physics-audit.md) | `66da33fd6` | Acht reproduzierte Fehlerszenarien; 15 Befunde, Runtime-Schnittstellen und gezielte Bereinigung |
| `src/platform` | [Platform Audit und Übergabe](../reports/2026-10-16-src-platform-audit.md) | `66da33fd6` | Zwölf Befunde; fünf isolierte Fehlerfälle reproduziert, sieben vorhandene Checks grün; Runtime-Randbefunde getrennt zugeordnet |
| `src/runtime` | [Runtime Audit und Übergabe](2026-10-16-src-runtime-audit.md) | `66da33fd6` | Separater Bericht eines anderen Agenten; dessen eigene Evidenz und Grenzen gelten |

Die Tabelle ist der maßgebliche Fundweg; bestehende Berichte liegen sowohl in
`docs/reviews` als auch in `docs/reports`. Weitere Berichte können dem Dateimuster
`YYYY-MM-DD-src-<bereich>-audit.md` folgen. Bei verschachtelten
Pfaden eignet sich beispielsweise `graphics-rhi` als Bereich. Für die gemeinsame
Auswertung sind folgende Angaben hilfreich:

- Originalfragen, geprüfter Commit, Umfang und tatsächlich untersuchte Aufrufer.
- Stabile lokale Befundkennungen, Priorität, konkreter Auslöser, Fundstelle und Auswirkung.
- Trennung zwischen ausgeführter Reproduktion, Quellbefund, Integrationslücke und Empfehlung.
- Kleinste Korrektur, betroffene Tests und bereits bestehende Aufgabenbesitzer.
- Tatsächlich ausgeführte Prüfungen und Grenzen der Aussage.

Die Befundkennungen sind keine Task-IDs. Der Index eröffnet keine neuen Aufgaben
und ersetzt keine bestehende Aufgaben- oder Architekturautorität.
