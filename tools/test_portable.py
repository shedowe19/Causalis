#!/usr/bin/env python3
"""Build and verify the portable core with a C++20 GCC compiler; no third-party Python deps."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--disable-leak-check", action="store_true",
                        help="Use only where LeakSanitizer cannot inspect the process")
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = args.build_dir.resolve() if args.build_dir else Path(tempfile.mkdtemp(prefix="causalis-tests-"))
    build.mkdir(parents=True, exist_ok=True)
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-I", str(root / "include")]
    flags += ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if args.sanitize else ["-O2"]
    sources = ["engine", "document", "script", "page", "checkpoint", "vault_policy"]
    objects = []
    for name in sources:
        target = build / (name + ".o")
        subprocess.run([args.compiler, *flags, "-c", str(root / "src" / (name + ".cpp")), "-o", str(target)], check=True)
        objects.append(str(target))
    env = os.environ.copy()
    if args.disable_leak_check:
        env["ASAN_OPTIONS"] = "detect_leaks=0"
    env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    tests = ["engine_tests", "document_tests", "script_tests", "page_tests", "vault_policy_tests", "fuzz_smoke"]
    for name in tests:
        target = build / name
        subprocess.run([args.compiler, *flags, str(root / "tests" / (name + ".cpp")), *objects, "-o", str(target)], check=True)
        subprocess.run([str(target)], check=True, env=env, timeout=90)
    cli = build / "causalis-cli"
    subprocess.run([args.compiler, *flags, str(root / "src" / "cli.cpp"), *objects, "-o", str(cli)], check=True)
    for sample in ["start", "decisions", "script-demo"]:
        for options in [[], ["--reading"], ["--run-scripts", "--source"]]:
            output = subprocess.check_output([str(cli), str(root / "samples" / (sample + ".html")), "640", *options], env=env, timeout=30)
            frame = json.loads(output)
            assert frame["engine"] == "Causalis 0.2.0" and frame["commands"]
            if sample == "script-demo" and "--run-scripts" in options:
                assert frame["script_console"] == ["Summe 15"], frame["script_console"]
                assert "Berechnet: 15" in frame["document_html"]
                assert not frame["script_diagnostics"], frame["script_diagnostics"]
    invalid = build / "invalid-utf8.html"
    invalid.write_bytes(b"<p>\xff</p>")
    failed = subprocess.run([str(cli), str(invalid)], env=env, capture_output=True)
    assert failed.returncode == 1 and b"valid UTF-8" in failed.stderr
    failed = subprocess.run([str(cli), str(root / "samples" / "start.html"), "nan"], env=env, capture_output=True)
    assert failed.returncode == 1 and b"WIDTH" in failed.stderr
    print("Portable suites, 9 CLI sample modes, invalid UTF-8 and invalid width passed.")
    print("Build directory:", build)


if __name__ == "__main__":
    main()
