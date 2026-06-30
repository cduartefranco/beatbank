#!/usr/bin/env python3
"""
validate_patterns.py — strict validator for Beat Bank .beat files.

Checks every pattern in src/patterns/*.beat (or a dir given as argv[1]):
  - has name, genre, steps
  - steps is 16 or 32
  - every voice row is EXACTLY `steps` chars (raw, no auto-padding)
  - rows use only the chars . x A g X o
  - voice keys are known
  - at least one hit
  - no duplicate names

Exits non-zero on any problem. Prints a genre histogram and total count.
"""
import sys, os, glob, collections

VOICES = {"kick","snare","ch","oh","clap","rim","tom","ride","crash","cowbell","conga","perc"}
VALID_CHARS = set(".xAgXo")

def main():
    d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "src", "patterns")
    files = sorted(glob.glob(os.path.join(d, "*.beat")))
    if not files:
        print(f"no .beat files in {d}"); return 1

    errors = []
    names = collections.Counter()
    genres = collections.Counter()
    total = 0

    for path in files:
        cur = None
        def finish(cur):
            nonlocal total
            if cur is None: return
            name, genre, steps, rows, line = cur
            where = f"{os.path.basename(path)}:{line} '{name or '?'}'"
            if not name: errors.append(f"{where}: missing name")
            if not genre: errors.append(f"{where}: missing genre")
            if steps not in (16, 32): errors.append(f"{where}: steps={steps} (must be 16 or 32)")
            if not rows: errors.append(f"{where}: no voice rows")
            hits = 0
            for v, row in rows:
                if v not in VOICES: errors.append(f"{where}: unknown voice '{v}'")
                if len(row) != steps:
                    errors.append(f"{where}: voice '{v}' len {len(row)} != steps {steps}  [{row}]")
                bad = set(row) - VALID_CHARS
                if bad: errors.append(f"{where}: voice '{v}' bad chars {bad}")
                hits += sum(1 for c in row if c in "xAgXo")
            if hits == 0: errors.append(f"{where}: no hits")
            names[name] += 1
            genres[genre] += 1
            total += 1

        with open(path) as f:
            for ln, raw in enumerate(f, 1):
                line = raw.strip()
                if not line or line.startswith("#"): continue
                if ":" not in line: continue
                key, val = line.split(":", 1)
                key = key.strip(); val = val.strip()
                if key == "name":
                    finish(cur); cur = [val, "", 16, [], ln]
                elif cur is None:
                    errors.append(f"{os.path.basename(path)}:{ln}: '{key}' before any name")
                elif key == "genre": cur[1] = val
                elif key == "steps":
                    try: cur[2] = int(val)
                    except ValueError: errors.append(f"{os.path.basename(path)}:{ln}: bad steps '{val}'")
                elif key in VOICES or key not in ("name","genre","steps"):
                    cur[3].append((key, val.replace(" ", "")))
            finish(cur)

    dups = [n for n, c in names.items() if c > 1]
    if dups: errors.append(f"duplicate names: {dups}")

    print(f"Files: {len(files)}   Patterns: {total}")
    print("Genres: " + ", ".join(f"{g}:{c}" for g, c in sorted(genres.items())))
    if errors:
        print(f"\n{len(errors)} PROBLEM(S):")
        for e in errors: print("  " + e)
        return 1
    print("\nOK: all patterns valid")
    return 0

if __name__ == "__main__":
    sys.exit(main())
