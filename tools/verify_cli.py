#!/usr/bin/env python3
"""Verify an already built Causalis CLI, including native Windows builds."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def verify(binary, root, env=None):
    binary = Path(binary).resolve()
    root = Path(root).resolve()
    checks = []
    for sample in ["start", "decisions", "script-demo"]:
        for options in [[], ["--reading"], ["--run-scripts", "--source"]]:
            output = subprocess.check_output([str(binary), str(root / "samples" / (sample + ".html")), "640", *options], env=env, timeout=30)
            frame = json.loads(output)
            if frame["engine"] != "Causalis 0.2.0" or not frame["commands"]:
                raise RuntimeError("Unexpected engine version or empty display list")
            if sample == "script-demo":
                source = frame.get("document_html", "")
                if "--run-scripts" in options:
                    if frame["script_console"] != ["Summe 15"] or "Berechnet: 15" not in source or frame["script_diagnostics"]:
                        raise RuntimeError("Local script demonstration did not execute correctly")
                elif frame["script_console"]:
                    raise RuntimeError("Script executed without opt-in")
            checks.append({"sample": sample, "options": options, "commands": len(frame["commands"]), "passed": True})
    with tempfile.TemporaryDirectory(prefix="causalis-cli-fixtures-") as temporary:
        invalid = Path(temporary) / "invalid-utf8.html"
        invalid.write_bytes(b"<p>\xff</p>")
        result = subprocess.run([str(binary), str(invalid)], env=env, capture_output=True, timeout=30)
        if result.returncode != 1 or b"valid UTF-8" not in result.stderr:
            raise RuntimeError("Invalid UTF-8 was not rejected")
        checks.append({"negative_case": "invalid-utf8", "passed": True})
    result = subprocess.run([str(binary), str(root / "samples" / "start.html"), "nan"], env=env, capture_output=True, timeout=30)
    if result.returncode != 1 or b"WIDTH" not in result.stderr:
        raise RuntimeError("Invalid viewport width was not rejected")
    checks.append({"negative_case": "invalid-width", "passed": True})
    return {"passed": True, "checks": checks}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    try:
        report = verify(args.binary, Path(__file__).resolve().parents[1], os.environ.copy())
    except Exception as error:
        report = {"passed": False, "error": str(error)}
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if not report["passed"]:
        print("CLI verification failed: " + report["error"], file=sys.stderr)
        return 1
    print("9 CLI sample modes and 2 invalid-input checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
