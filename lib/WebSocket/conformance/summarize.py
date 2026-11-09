#!/usr/bin/env python3
"""Summarize an Autobahn index.json: counts per behavior and the non-OK cases.

Exits with 1 if any case failed (behavior or close behavior other than OK,
NON-STRICT or INFORMATIONAL) or if the report has no cases, so it can gate CI.
"""
import collections
import json
import sys

ACCEPTED = ("OK", "NON-STRICT", "INFORMATIONAL")
ACCEPTED_CLOSE = ("OK", "INFORMATIONAL")


def case_key(case_id):
    return [int(part) for part in case_id.split(".")]


def main(path):
    data = json.load(open(path))
    ok = bool(data)
    for agent, cases in data.items():
        counts = collections.Counter()
        bad = []
        for cid in sorted(cases, key=case_key):
            r = cases[cid]
            counts[r["behavior"]] += 1
            if r["behavior"] not in ACCEPTED or r["behaviorClose"] not in ACCEPTED_CLOSE:
                bad.append(f"  {cid:10} {r['behavior']:14} close={r['behaviorClose']} {r['duration']}ms")
        print(agent, dict(counts))
        if bad:
            print("\n".join(bad))
        ok = ok and bool(cases) and not bad
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
