"""Optionally copy valid questions from the original project's SQLite database.

Run after starting the new server once to create its schema, then stop that server.
Accounts and old rooms are intentionally not copied: their session model differs.
"""
import sqlite3
import sys
from pathlib import Path

if len(sys.argv) != 3:
    raise SystemExit("Usage: python tools/import_questions.py OLD_DB NEW_DB")

old_path, new_path = map(Path, sys.argv[1:])
if not old_path.is_file() or not new_path.is_file():
    raise SystemExit("Both database files must exist.")

old = sqlite3.connect(f"file:{old_path.resolve().as_posix()}?mode=ro", uri=True)
new = sqlite3.connect(new_path)
rows = old.execute("""
    SELECT q.text,q.option_a,q.option_b,q.option_c,q.option_d,
           q.correct_option,t.name,d.name
    FROM questions q JOIN topics t ON t.id=q.topic_id
    JOIN difficulties d ON d.id=q.difficulty_id
""").fetchall()
added = skipped = 0
with new:
    for row in rows:
        if any(not isinstance(x, str) or not x or "|" in x or "\n" in x or "\r" in x for x in row):
            skipped += 1
            continue
        if row[5] not in "ABCD":
            skipped += 1
            continue
        if new.execute("SELECT 1 FROM questions WHERE text=? AND a=? AND b=? AND c=? AND d=?", row[:5]).fetchone():
            skipped += 1
            continue
        new.execute("INSERT INTO questions(text,a,b,c,d,correct,topic,difficulty) VALUES(?,?,?,?,?,?,?,?)", row)
        added += 1
print(f"Imported {added} questions; skipped {skipped} invalid or duplicate questions.")
