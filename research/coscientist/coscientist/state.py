"""Persistent state: hypotheses, results, Elo ratings, match history, meta-review feedback.
SQLite so a run survives crashes and can be inspected / restarted."""
from __future__ import annotations
import sqlite3, json, os, time

INITIAL_ELO = 1200.0

SCHEMA = """
CREATE TABLE IF NOT EXISTS hypotheses(
  id TEXT PRIMARY KEY, goal_id TEXT, kind TEXT, text TEXT, manifest TEXT,
  parent TEXT, agent TEXT, round INT, ts REAL);
CREATE TABLE IF NOT EXISTS results(
  cand_id TEXT PRIMARY KEY, gate_passed INT, fitness REAL, record TEXT, review TEXT);
CREATE TABLE IF NOT EXISTS elo(cand_id TEXT PRIMARY KEY, rating REAL);
CREATE TABLE IF NOT EXISTS matches(
  id INTEGER PRIMARY KEY AUTOINCREMENT, a TEXT, b TEXT, winner TEXT,
  kind TEXT, round INT, ts REAL);
CREATE TABLE IF NOT EXISTS meta(round INT, text TEXT, ts REAL);
"""


class Store:
    def __init__(self, path: str):
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        self.db = sqlite3.connect(path)
        self.db.executescript(SCHEMA)
        self.db.commit()

    # --- hypotheses ---
    def add_hypothesis(self, h: dict):
        self.db.execute(
            "INSERT OR REPLACE INTO hypotheses VALUES(?,?,?,?,?,?,?,?,?)",
            (h["id"], h.get("goal_id"), h.get("kind"), h.get("text", ""),
             json.dumps(h.get("manifest", {})), h.get("parent"), h.get("agent"),
             h.get("round", 0), time.time()))
        self.db.execute("INSERT OR IGNORE INTO elo VALUES(?,?)", (h["id"], INITIAL_ELO))
        self.db.commit()

    def hypothesis(self, cid: str) -> dict:
        r = self.db.execute("SELECT id,goal_id,kind,text,manifest,parent,agent,round "
                            "FROM hypotheses WHERE id=?", (cid,)).fetchone()
        keys = ["id", "goal_id", "kind", "text", "manifest", "parent", "agent", "round"]
        d = dict(zip(keys, r))
        d["manifest"] = json.loads(d["manifest"])
        return d

    # --- results ---
    def set_result(self, cand_id, record: dict, fitness, gate_passed, review=None):
        self.db.execute("INSERT OR REPLACE INTO results VALUES(?,?,?,?,?)",
                        (cand_id, int(bool(gate_passed)),
                         fitness if fitness is not None else None,
                         json.dumps(record), json.dumps(review) if review else None))
        self.db.commit()

    def record(self, cand_id) -> dict | None:
        r = self.db.execute("SELECT record FROM results WHERE cand_id=?",
                            (cand_id,)).fetchone()
        return json.loads(r[0]) if r else None

    def fitness(self, cand_id):
        r = self.db.execute("SELECT fitness FROM results WHERE cand_id=?",
                            (cand_id,)).fetchone()
        return r[0] if r else None

    def is_eligible(self, cand_id) -> bool:
        r = self.db.execute("SELECT gate_passed FROM results WHERE cand_id=?",
                            (cand_id,)).fetchone()
        return bool(r and r[0])

    def all_ids(self):
        return [r[0] for r in self.db.execute("SELECT cand_id FROM results").fetchall()]

    # --- elo ---
    def elo(self, cid) -> float:
        r = self.db.execute("SELECT rating FROM elo WHERE cand_id=?", (cid,)).fetchone()
        return r[0] if r else INITIAL_ELO

    def set_elo(self, cid, rating):
        self.db.execute("INSERT OR REPLACE INTO elo VALUES(?,?)", (cid, rating))
        self.db.commit()

    def record_match(self, a, b, winner, kind, rnd):
        self.db.execute("INSERT INTO matches(a,b,winner,kind,round,ts) VALUES(?,?,?,?,?,?)",
                        (a, b, winner, kind, rnd, time.time()))
        self.db.commit()

    # --- meta-review feedback ---
    def add_meta(self, rnd, text):
        self.db.execute("INSERT INTO meta VALUES(?,?,?)", (rnd, text, time.time()))
        self.db.commit()

    def latest_meta(self) -> str:
        r = self.db.execute("SELECT text FROM meta ORDER BY round DESC LIMIT 1").fetchone()
        return r[0] if r else ""

    # --- views ---
    def leaderboard(self):
        rows = self.db.execute(
            "SELECT h.id, e.rating, r.gate_passed, r.fitness "
            "FROM hypotheses h JOIN elo e ON h.id=e.cand_id "
            "LEFT JOIN results r ON h.id=r.cand_id "
            "ORDER BY e.rating DESC").fetchall()
        return [{"id": r[0], "elo": round(r[1], 1),
                 "gate_passed": bool(r[2]) if r[2] is not None else None,
                 "fitness": r[3]} for r in rows]
