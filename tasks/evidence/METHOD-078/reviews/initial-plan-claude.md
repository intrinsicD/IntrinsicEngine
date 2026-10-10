**Urteil: revise**

**1. Nutzerausrichtung – Blocker**
Gefragt ist „untersuchen und verbessern“. Der Plan ersetzt das Untersuchen durch Code-Lesen und behebt nur H1, ohne zu zeigen, dass H1 in METHOD-077 überhaupt greift. Dagegen spricht: CPD zentriert beide Mengen getrennt (`src/geometry/Geometry.Registration.CoherentPointDrift.cpp:681`). Die Testgeometrie „getrennt, dann überlappend“ kommt so in echten Läufen kaum vor. Bei fallendem σ² spricht mehr für H3, und dann bringt die Änderung nur zusätzliche Kosten. Diese Umdeutung und ihre erwartete Wirkung müssen zur Nutzerentscheidung vorgelegt werden.

**2. Minimalität**
Der Diff selbst ist minimal: ein Member und eine Schleifenbedingung. Das Risiko liegt in der Semantik.

**3. Regressionsdesign**
- `Variant()` setzt `NystromErrorLimit = 1.0` (`tests/unit/geometry/Test.CoherentPointDriftAccelerated.cpp:649`). Damit lehnt σ²=8 nicht wegen Genauigkeit ab. Das Limit 1e-3 explizit setzen und belegen, dass Aufruf 1 wirklich einen Versuch gemacht hat.
- „Ein frischer Evaluator akzeptiert“ heißt nur bei Mixed/Points gleicher Zustand. Dort bitweise Gleichheit von wiederverwendetem und frischem Evaluator fordern.
- Fixed (Identitäten der getrennten Quelle) und Resampled (Draw 1 statt 0) haben anderen Zustand. Dort die Akzeptanz direkt prüfen, mit Abstand zur Grenze.
- Box-Quellen ausschließen: Fixed-Box friert die getrennte Union ein (`…EStep.cpp:1356`).
- „Poorly separated“ quantifizieren: Offset und erwarteter Ablehnungsgrund.
- Test 2 streichen. Die Settings sind pro Solver konstant, und jedes `Initialize` erzeugt einen neuen Evaluator (`…CoherentPointDrift.cpp:623`, `:743`). Der Test dient also keiner Frage, und nach dem Entfernen bleibt ohnehin kein Zustand. H2 ist gewolltes Verhalten und schon getestet (`NystromRunsExactlyWhereItWouldNotPay`).

**4. Korrektheit – Blocker**
Mit der Sperre gibt es bei fallendem σ² höchstens einen Wechsel von Nyström zu exakt. Ohne sie kann der Tail zwischen beiden wechseln. Der Konvergenztest |ΔL| ≤ 1e-5·|L| (`…CoherentPointDrift.cpp:832`) steht gegen eine Fehlergrenze von 1e-3. Solche Wechsel können Konvergenz blockieren oder vortäuschen. Vorab als Assertions vorhersagen: Terminierung, Iterationszahl und Punktabweichung in `NystromRegistrationMatchesTheReference`.

**5. Performance – Blocker**
„Bounded“ gilt nur relativ zu dense. Im schmalen Tail läuft Truncated, also nur ein Bruchteil von NM. Ohne Sperre macht jede Tail-Iteration erneut einen Versuch, und zwar ab der zuletzt bestandenen Landmark-Zahl (`src/geometry/Geometry.Registration.CoherentPointDrift.EStep.cpp:1652`). Bei 10^5 Punkten und L=1024 sind das nach dem Kostenmodell des Codes etwa 0,25 NM pro Iteration, plus eine single-threaded Eigenzerlegung (`…EStep.cpp:1646`). Die dokumentierten 128 s vs. 198 s (`methods/geometry/coherent_point_drift/paper.md:191`) könnten dadurch kippen.

`KernelEvaluations` erfasst Ablehnungen per Early-Return nur teilweise und die Eigenzerlegung gar nicht (`ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/reviews/claude-r1b.md:48`). Kein bestehender Test würde den Mehraufwand also bemerken. Nötig ist eines von beiden:
- explizite Freigabe dieses Risikos, und die Paper-Zahlen an den alten Commit binden;
- oder ein Budget gegen die tatsächliche exakte Alternative statt gegen dense. Das ist eine Policy-Änderung, also ebenfalls Nutzerentscheidung.

**6. Verifikation und Doku**
- Direkt betroffen, aber nicht im Plan: `CoherentPointDriftOperations.Nystrom*` in `IntrinsicRuntimeContractTests` (`tests/contract/runtime/Test.CoherentPointDriftOperations.cpp:447`, `:468`). Diese Tests gehören in den fokussierten Lauf.
- Die Bayesian-Suite nutzt kein Nyström; sie mitlaufen zu lassen ist harmlos, aber unnötig.
- Veraltete Kommentare und Texte: `…EStep.cpp:732` und `:1674`, im Accelerated-Test `:244` und `:975`, `README.md:38` („then exact“), `paper.md:188`.