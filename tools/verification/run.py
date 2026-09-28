#!/usr/bin/env python3
"""Fail-closed, manifest-driven OpenRFS verification with owned evidence."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import resource
import shutil
import signal
import subprocess
import sys
import time
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
MANIFEST_PATH = ROOT / "verification" / "manifest.json"
RUNS = ROOT / "verification" / "runs"
NAME = re.compile(r"^[a-z][a-z0-9-]*$")
PLACEHOLDERS = {"jobdir", "corpus", "artifacts", "input", "dictionary"}
REQUIRED = {
    "name", "subsystem", "production_files", "production_symbols",
    "harness_source", "engine", "sanitizers", "build_command",
    "smoke_command", "extended_command", "nightly_command",
    "replay_command", "seed_corpus", "dictionary", "input_max_bytes",
    "timeout_seconds", "memory_max_mb", "disk_max_mb", "oracle",
    "known_valid", "known_invalid", "execution", "coverage_method",
    "status", "known_gaps", "artifact_owner", "cleanup_behavior",
    "profiles", "required_tools",
}
INTERRUPTED = False


def utc() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def atomic_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
    temporary.replace(path)


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def tool_version(tool: str) -> str | None:
    executable = shutil.which(tool)
    if executable is None:
        return None
    try:
        result = subprocess.run([executable, "--version"], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=10, check=False)
    except (OSError, subprocess.TimeoutExpired):
        return "version unavailable"
    return " | ".join(result.stdout.splitlines()[:5]) if result.stdout else "version unavailable"


def load_manifest() -> dict[str, Any]:
    value = json.loads(MANIFEST_PATH.read_text())
    if value.get("schema_version") != 1 or not isinstance(value.get("targets"), list):
        raise ValueError("unsupported verification manifest")
    return value


def command_tokens(command: Any, label: str) -> None:
    if command is None:
        return
    if not isinstance(command, list) or not command or not all(
        isinstance(item, str) and item for item in command
    ):
        raise ValueError(f"{label}: command must be nonempty argv")
    for token in command:
        for placeholder in re.findall(r"\{([^{}]+)\}", token):
            if placeholder not in PLACEHOLDERS:
                raise ValueError(f"{label}: unknown placeholder {placeholder}")


def availability(target: dict[str, Any]) -> list[str]:
    unavailable = []
    for tool, expected in target["required_tools"].items():
        version = tool_version(tool)
        if version is None:
            unavailable.append(f"{tool} missing")
        elif expected == "present":
            continue
        elif expected.startswith(">="):
            found = re.search(r"\d+(?:\.\d+)+", version)
            wanted = tuple(int(part) for part in expected[2:].split("."))
            actual = tuple(int(part) for part in found.group().split(".")) if found else ()
            if actual < wanted:
                unavailable.append(f"{tool} {version!r} < {expected}")
        elif expected not in version:
            unavailable.append(f"{tool} {version!r} lacks {expected!r}")
    for module, expected in target.get("required_python_modules", {}).items():
        try:
            found = subprocess.check_output(
                ["python3", "-c", f"import {module}; print({module}.__version__)"],
                text=True, timeout=10).strip()
        except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired):
            unavailable.append(f"Python module {module} missing")
        else:
            if found != expected:
                unavailable.append(f"Python module {module} is {found}, expected {expected}")
    return unavailable


def validate_manifest(manifest: dict[str, Any], *, check_tools: bool,
                      profile: str | None = None) -> list[str]:
    errors: list[str] = []
    names: set[str] = set()
    for target in manifest["targets"]:
        missing = REQUIRED - target.keys()
        name = target.get("name", "<unnamed>")
        if missing:
            errors.append(f"{name}: missing fields {sorted(missing)}")
            continue
        if not isinstance(name, str) or not NAME.fullmatch(name) or name in names:
            errors.append(f"invalid or duplicate target name: {name}")
            continue
        names.add(name)
        for field in ("production_files", "harness_source", "seed_corpus", "dictionary"):
            paths = target[field] if field == "production_files" else [target[field]]
            for entry in paths:
                if entry is None:
                    continue
                path = ROOT / entry
                if Path(entry).is_absolute() or ".." in Path(entry).parts or not path.exists():
                    errors.append(f"{name}: missing or unsafe {field}: {entry}")
        corpus = ROOT / target["seed_corpus"] if target["seed_corpus"] else None
        if corpus is not None:
            if not corpus.is_dir() or not any(corpus.iterdir()):
                errors.append(f"{name}: empty corpus")
            for seed in target["known_valid"] + target["known_invalid"]:
                if Path(seed).name != seed or not (corpus / seed).is_file():
                    errors.append(f"{name}: missing seed {seed}")
                elif target["input_max_bytes"] is not None and \
                        (corpus / seed).stat().st_size > target["input_max_bytes"]:
                    errors.append(f"{name}: seed exceeds input limit: {seed}")
        sources = [ROOT / entry for entry in target["production_files"]]
        for symbol in target["production_symbols"]:
            if not any(path.is_file() and symbol in path.read_text(errors="replace")
                       for path in sources):
                errors.append(f"{name}: production symbol not found: {symbol}")
        for field in ("build_command", "smoke_command", "extended_command",
                      "nightly_command", "replay_command"):
            try:
                command_tokens(target[field], f"{name}.{field}")
            except ValueError as error:
                errors.append(str(error))
        for target_profile in target["profiles"]:
            field = "smoke_command" if target_profile == "fast" else f"{target_profile}_command"
            if target.get(field) is None or target_profile not in target["timeout_seconds"]:
                errors.append(f"{name}: profile {target_profile} lacks command or timeout")
        if target["status"] == "integrated" and check_tools and \
                (profile is None or profile in target["profiles"]):
            errors.extend(f"{name}: {reason}" for reason in availability(target))
    return errors


def expand(command: list[str], values: dict[str, str]) -> list[str]:
    return [token.format_map(values) for token in command]


def bytes_in(directory: Path) -> int:
    return sum(path.stat().st_size for path in directory.rglob("*") if path.is_file())


def on_interrupt(_number: int, _frame: Any) -> None:
    global INTERRUPTED
    INTERRUPTED = True


def run_step(label: str, command: list[str], output: Path, timeout: int,
             *, extra_env: dict[str, str] | None = None,
             file_limit_mb: int = 64, memory_mb: int | None = None) -> dict[str, Any]:
    output.mkdir(parents=True, exist_ok=True)
    stdout = output / f"{label}.stdout.log"
    stderr = output / f"{label}.stderr.log"
    start = time.monotonic()
    record: dict[str, Any] = {
        "name": label, "command": command, "started_utc": utc(),
        "timeout_limit_seconds": timeout,
        "file_limit_mb": file_limit_mb, "address_space_limit_mb": memory_mb,
        "stdout": str(stdout.relative_to(ROOT)),
        "stderr": str(stderr.relative_to(ROOT)),
    }
    environment = os.environ.copy()
    environment["LLVM_PROFILE_FILE"] = str(output / f"{label}-%p.profraw")
    environment.update(extra_env or {})

    def limits() -> None:
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        file_bytes = file_limit_mb * 1024 * 1024
        resource.setrlimit(resource.RLIMIT_FSIZE, (file_bytes, file_bytes))
        resource.setrlimit(resource.RLIMIT_CPU, (timeout + 10, timeout + 10))
        resource.setrlimit(resource.RLIMIT_NOFILE, (256, 256))
        if memory_mb is not None:
            address_bytes = memory_mb * 1024 * 1024
            resource.setrlimit(resource.RLIMIT_AS, (address_bytes, address_bytes))

    with stdout.open("wb") as out, stderr.open("wb") as err:
        try:
            child = subprocess.Popen(command, cwd=ROOT, stdout=out, stderr=err,
                                     env=environment, start_new_session=True,
                                     preexec_fn=limits)
        except FileNotFoundError as error:
            record.update(status="unavailable", tool_error=str(error))
        except (OSError, subprocess.SubprocessError) as error:
            record.update(status="infra_error", tool_error=str(error))
        else:
            deadline = start + timeout
            while child.poll() is None and not INTERRUPTED and time.monotonic() < deadline:
                try:
                    child.wait(timeout=min(1, max(0.01, deadline - time.monotonic())))
                except subprocess.TimeoutExpired:
                    pass
            if child.poll() is None:
                os.killpg(child.pid, signal.SIGTERM)
                try:
                    child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(child.pid, signal.SIGKILL)
                    child.wait()
                record.update(status="interrupted" if INTERRUPTED else "timed_out",
                              timeout=not INTERRUPTED, exit_code=child.returncode,
                              signal=-child.returncode if child.returncode < 0 else None)
            else:
                code = child.returncode
                record.update(status="passed" if code == 0 else "failed_finding",
                              exit_code=code, signal=-code if code < 0 else None,
                              timeout=False)
    record["finished_utc"] = utc()
    record["elapsed_seconds"] = round(time.monotonic() - start, 3)
    record["stdout_sha256"] = digest(stdout)
    record["stderr_sha256"] = digest(stderr)
    return record


def corpus_digest(corpus: Path) -> str:
    summary = hashlib.sha256()
    for path in sorted(item for item in corpus.iterdir() if item.is_file()):
        summary.update(path.name.encode())
        summary.update(bytes.fromhex(digest(path)))
    return summary.hexdigest()


def collect_coverage(target: dict[str, Any], jobdir: Path,
                     steps: list[dict[str, Any]]) -> dict[str, Any]:
    corpus = jobdir / "corpus"
    raw = jobdir / "profiles"
    raw.mkdir()
    binary = jobdir / "bin" / "replay"
    for index, seed in enumerate(sorted(corpus.iterdir())):
        profile = raw / f"{index}.profraw"
        step = run_step(f"coverage-{index}", [str(binary), str(seed)],
                        jobdir / "logs", 10,
                        extra_env={"LLVM_PROFILE_FILE": str(profile)})
        steps.append(step)
        if step["status"] != "passed":
            raise RuntimeError(f"coverage replay failed: {seed.name}")
    merged = jobdir / "coverage.profdata"
    merge = run_step("coverage-merge", ["llvm-profdata-18", "merge", "-sparse",
                       *[str(path) for path in sorted(raw.glob("*.profraw"))],
                       "-o", str(merged)], jobdir / "logs", 30)
    steps.append(merge)
    if merge["status"] != "passed":
        raise RuntimeError("LLVM profile merge failed")
    export = run_step("coverage-export", ["llvm-cov-18", "export", str(binary),
                        f"-instr-profile={merged}"], jobdir / "logs", 30)
    steps.append(export)
    if export["status"] != "passed":
        raise RuntimeError("LLVM coverage export failed")
    value = json.loads((jobdir / "logs" / "coverage-export.stdout.log").read_text())
    data = value["data"][0]
    files = []
    for item in data.get("files", []):
        filename = item["filename"]
        if any(filename.endswith(path) for path in target["production_files"]):
            files.append({"file": filename, "summary": item.get("summary", {})})
    functions = []
    for item in data.get("functions", []):
        filename = item.get("filenames", [""])[0]
        if any(filename.endswith(path) for path in target["production_files"]):
            functions.append({"name": item["name"], "count": item.get("count", 0)})
    result = {"production_files": files, "production_functions": functions,
              "method": "Clang 18 source profile over saved corpus",
              "profile_sha256": digest(merged)}
    atomic_json(jobdir / "coverage-summary.json", result)
    return result


def collect_fuzz_metrics(step: dict[str, Any], corpus: Path) -> dict[str, Any]:
    stderr = (ROOT / step["stderr"]).read_text(errors="replace")
    executed = re.findall(r"stat::number_of_executed_units:\s*(\d+)", stderr)
    done = re.findall(r"#\d+\s+DONE\s+cov:\s*(\d+)\s+ft:\s*(\d+)", stderr)
    count = int(executed[-1]) if executed else None
    elapsed = step["elapsed_seconds"]
    return {
        "executions": count, "elapsed_seconds": elapsed,
        "executions_per_second": round(count / elapsed, 1) if count and elapsed else None,
        "edges": int(done[-1][0]) if done else None,
        "features": int(done[-1][1]) if done else None,
        "corpus_files": sum(path.is_file() for path in corpus.iterdir()),
        "corpus_bytes": bytes_in(corpus), "corpus_sha256": corpus_digest(corpus),
    }


def collect_property_metrics(step: dict[str, Any], corpus: Path) -> dict[str, Any]:
    stdout = (ROOT / step["stdout"]).read_text(errors="replace")
    attempts = re.search(r"; (\d+) executed; seed=(\d+)", stdout)
    counts = re.search(r"^operation_counts=(\{.*\})$", stdout, re.M)
    return {"executions": int(attempts.group(1)) if attempts else None,
            "seed": int(attempts.group(2)) if attempts else None,
            "operation_counts": json.loads(counts.group(1)) if counts else None,
            "elapsed_seconds": step["elapsed_seconds"],
            "corpus_files": sum(path.is_file() for path in corpus.iterdir()),
            "corpus_sha256": corpus_digest(corpus)}


def run_target(target: dict[str, Any], profile: str, run_dir: Path) -> dict[str, Any]:
    name = target["name"]
    jobdir = run_dir / name
    if jobdir.exists():
        attempt = 2
        while (run_dir / f"{name}-attempt-{attempt}").exists():
            attempt += 1
        jobdir = run_dir / f"{name}-attempt-{attempt}"
    jobdir.mkdir()
    (jobdir / "artifacts").mkdir()
    steps: list[dict[str, Any]] = []
    result: dict[str, Any] = {"name": name, "status": "pending", "steps": steps,
                              "started_utc": utc(), "evidence_dir": str(jobdir.relative_to(ROOT))}
    unavailable = availability(target)
    if unavailable:
        result.update(status="unavailable", reasons=unavailable, finished_utc=utc())
        return result
    values = {"jobdir": str(jobdir), "artifacts": str(jobdir / "artifacts"),
              "corpus": str(jobdir / "corpus"),
              "dictionary": str(ROOT / target["dictionary"]) if target["dictionary"] else "",
              "input": ""}
    if target["seed_corpus"]:
        shutil.copytree(ROOT / target["seed_corpus"], jobdir / "corpus")
        result["seed_corpus_sha256"] = corpus_digest(jobdir / "corpus")
    if target["build_command"] is not None:
        build = run_step("build", expand(target["build_command"], values),
                         jobdir / "logs", 900,
                         file_limit_mb=target["disk_max_mb"])
        steps.append(build)
        if build["status"] != "passed":
            result.update(status=build["status"], finished_utc=utc())
            return result
    if target["seed_corpus"]:
        for valid, names in ((True, target["known_valid"]),
                             (False, target["known_invalid"])):
            for seed in names:
                values["input"] = str(jobdir / "corpus" / seed)
                replay = run_step(f"health-{seed}",
                                  expand(target["replay_command"], values),
                                  jobdir / "logs", 10)
                replay["input_sha256"] = digest(Path(values["input"]))
                steps.append(replay)
                if replay["status"] == "interrupted":
                    result.update(status="interrupted", finished_utc=utc())
                    return result
                expected = f"status=0 accepted=1" if valid else "accepted=0"
                output = (ROOT / replay["stdout"]).read_text(errors="replace")
                if replay["status"] != "passed" or expected not in output:
                    result.update(status="failed_finding", finished_utc=utc(),
                                  reason=f"harness health failed: {seed}")
                    return result
        if target["engine"].startswith("LLVM libFuzzer"):
            try:
                result["coverage"] = collect_coverage(target, jobdir, steps)
            except (RuntimeError, ValueError, KeyError) as error:
                result.update(status="interrupted" if INTERRUPTED else "infra_error",
                              reason=str(error), finished_utc=utc())
                return result
            reached = {item["name"] for item in result["coverage"]["production_functions"]
                       if item["count"] > 0}
            if not reached.intersection(target["production_symbols"]):
                result.update(status="failed_finding",
                              reason="saved corpus reached no declared production symbol",
                              finished_utc=utc())
                return result
            covered = sum(item["summary"].get("regions", {}).get("covered", 0)
                          for item in result["coverage"]["production_files"])
            if covered < target.get("minimum_covered_regions", 0):
                result.update(status="failed_finding",
                              reason=f"harness health covers only {covered} production regions",
                              finished_utc=utc())
                return result
    field = "smoke_command" if profile == "fast" else f"{profile}_command"
    command = expand(target[field], values)
    campaign = run_step(profile, command, jobdir / "logs",
                        target["timeout_seconds"][profile],
                        extra_env={"OPENRFS_FAILURE_DIR": str(jobdir / "artifacts")},
                        file_limit_mb=target["disk_max_mb"],
                        memory_mb=None if target["engine"].startswith("LLVM libFuzzer")
                        else target["memory_max_mb"])
    steps.append(campaign)
    if target["engine"].startswith("LLVM libFuzzer"):
        result["fuzz_metrics"] = collect_fuzz_metrics(campaign, jobdir / "corpus")
    elif target["engine"].startswith("Hypothesis"):
        result["property_metrics"] = collect_property_metrics(campaign,
                                                                 jobdir / "corpus")
    elif target["execution"].startswith("QEMU guest"):
        scenario = ROOT / "build" / "tests" / "normal"
        for filename in ("openrfs.iso", "serial.log", "scenario-result.json"):
            source = scenario / filename
            if source.is_file():
                destination = jobdir / filename
                shutil.copyfile(source, destination)
                result[filename] = {"sha256": digest(destination),
                                    "bytes": destination.stat().st_size}
    if bytes_in(jobdir) > target["disk_max_mb"] * 1024 * 1024:
        result.update(status="infra_error", reason="job evidence exceeded disk limit")
    else:
        result["status"] = campaign["status"]
    if campaign["status"] == "failed_finding":
        try:
            from finding import collect_finding
            result["finding"] = collect_finding(target, profile, jobdir,
                                                  campaign, source_snapshot(load_manifest()))
        except (OSError, ValueError, subprocess.CalledProcessError) as error:
            result["finding_error"] = str(error)
    result["finished_utc"] = utc()
    return result


def source_snapshot(manifest: dict[str, Any]) -> dict[str, Any]:
    tools = sorted({tool for target in manifest["targets"]
                    for tool in target["required_tools"]})
    return {
        "commit": git("rev-parse", "HEAD"),
        "tree": git("rev-parse", "HEAD^{tree}"),
        "dirty_paths": git("status", "--porcelain=v1").splitlines(),
        "manifest_sha256": digest(MANIFEST_PATH),
        "tools": {tool: tool_version(tool) for tool in tools},
        "machine": {"platform": platform.platform(), "cpu_count": os.cpu_count(),
                    "memory_bytes": os.sysconf("SC_PHYS_PAGES") *
                                    os.sysconf("SC_PAGE_SIZE"),
                    "qemu_acceleration": "tcg"},
    }


def perform(manifest: dict[str, Any], profile: str, selected: list[str],
            resume: Path | None = None) -> int:
    targets = [target for target in manifest["targets"]
               if target["name"] in selected or
               (not selected and profile in target["profiles"])]
    if selected and len(targets) != len(selected):
        raise ValueError("unknown target name")
    if any(profile not in target["profiles"] for target in targets):
        raise ValueError("target does not support requested profile")
    source = source_snapshot(manifest)
    if profile in ("extended", "nightly") and source["dirty_paths"]:
        raise ValueError("extended and nightly verification require a clean exact HEAD")
    RUNS.mkdir(parents=True, exist_ok=True)
    if resume is not None:
        run_dir = resume.resolve(strict=True)
        if run_dir.parent != RUNS.resolve():
            raise ValueError("resume directory is not an owned verification run")
        report = json.loads((run_dir / "run.json").read_text())
        if report["profile"] != profile or report["target_list"] != [item["name"] for item in targets]:
            raise ValueError("resume target list or profile changed")
        if report["source"]["commit"] != source["commit"] or \
                report["source"]["manifest_sha256"] != source["manifest_sha256"] or \
                report["source"]["dirty_paths"] or source["dirty_paths"]:
            raise ValueError("resume requires the same clean source and manifest")
        if report["status"] != "interrupted":
            raise ValueError("only an interrupted run can be resumed")
        report["status"] = "running"
        report.setdefault("resumed_utc", []).append(utc())
    else:
        run_dir = RUNS / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") +
                          f"-{os.getpid()}-{time.time_ns() % 1000000:06d}")
        run_dir.mkdir()
        report = {"schema_version": 1, "profile": profile,
            "started_utc": utc(), "source": source,
            "target_list": [item["name"] for item in targets], "results": [],
            "unfinished": [item["name"] for item in targets], "status": "running"}
    atomic_json(run_dir / "run.json", report)
    signal.signal(signal.SIGINT, on_interrupt)
    signal.signal(signal.SIGTERM, on_interrupt)
    lock = RUNS / ".runner.lock"
    with lock.open("w") as stream:
        fcntl.flock(stream.fileno(), fcntl.LOCK_EX)
        for target in targets:
            if INTERRUPTED:
                break
            if target["name"] not in report["unfinished"]:
                continue
            print(f"[{utc()}] {target['name']}: {profile}", flush=True)
            result = run_target(target, profile, run_dir)
            report["results"].append(result)
            if result["status"] != "interrupted":
                report["unfinished"].remove(target["name"])
            atomic_json(run_dir / "run.json", report)
            print(f"  {result['status']}", flush=True)
    report["finished_utc"] = utc()
    latest = {item["name"]: item for item in report["results"]}
    report["status"] = "interrupted" if INTERRUPTED else (
        "passed" if not report["unfinished"] and all(
            item["status"] == "passed" for item in latest.values())
        else "failed")
    if profile in ("extended", "nightly"):
        report["ending_source"] = source_snapshot(manifest)
        if report["ending_source"]["commit"] != report["source"]["commit"] or \
                report["ending_source"]["manifest_sha256"] != report["source"]["manifest_sha256"] or \
                report["ending_source"]["dirty_paths"]:
            report["status"] = "invalidated"
            report["invalidated_reason"] = "source changed during exact-head verification"
    atomic_json(run_dir / "run.json", report)
    print(f"run: {run_dir}\nstatus: {report['status']}\n"
          f"commit: {report['source']['commit']}", flush=True)
    return 0 if report["status"] == "passed" else (130 if INTERRUPTED else 1)


def recover_stale(run_dir: Path) -> dict[str, Any]:
    """Classify a runner killed outside its signal handler as interrupted.

    The advisory lock proves that no runner still owns this evidence directory.
    Operators must also check for an orphaned child before invoking recovery.
    Existing results and unfinished targets are never converted to passes.
    """
    run_dir = run_dir.resolve(strict=True)
    if run_dir.parent != RUNS.resolve():
        raise ValueError("recovery directory is not an owned verification run")
    with (RUNS / ".runner.lock").open("w") as stream:
        try:
            fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise ValueError("runner is still active; recovery refused") from error
        report = json.loads((run_dir / "run.json").read_text())
        if report.get("status") != "running" or not report.get("unfinished"):
            raise ValueError("only a stale unfinished running receipt can be recovered")
        report["status"] = "interrupted"
        report["finished_utc"] = utc()
        report["recovery_note"] = (
            "Runner exited without final receipt; operator verified no orphaned child"
        )
        atomic_json(run_dir / "run.json", report)
        return report


def replay_one(manifest: dict[str, Any], name: str, input_path: Path) -> int:
    matches = [item for item in manifest["targets"] if item["name"] == name]
    if len(matches) != 1 or matches[0]["replay_command"] is None:
        raise ValueError("unknown or nonreplayable target")
    target = matches[0]
    input_path = input_path.resolve(strict=True)
    if not input_path.is_file() or input_path.stat().st_size > target["input_max_bytes"]:
        raise ValueError("input is not a file within the target's byte limit")
    reasons = availability(target)
    if reasons:
        print("unavailable: " + "; ".join(reasons), file=sys.stderr)
        return 2
    RUNS.mkdir(parents=True, exist_ok=True)
    run_dir = RUNS / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") +
                      f"-replay-{os.getpid()}-{time.time_ns() % 1000000:06d}")
    run_dir.mkdir()
    jobdir = run_dir / name
    jobdir.mkdir()
    saved = jobdir / "input"
    shutil.copyfile(input_path, saved)
    values = {"jobdir": str(jobdir), "input": str(saved), "corpus": "",
              "dictionary": str(ROOT / target["dictionary"]) if target["dictionary"] else "",
              "artifacts": str(jobdir / "artifacts")}
    steps = []
    if target["build_command"] is not None:
        steps.append(run_step("build", expand(target["build_command"], values),
                              jobdir / "logs", 900))
    if not steps or steps[-1]["status"] == "passed":
        steps.append(run_step("replay", expand(target["replay_command"], values),
                              jobdir / "logs", 10))
    status = steps[-1]["status"]
    report = {"schema_version": 1, "profile": "replay", "status": status,
              "started_utc": steps[0]["started_utc"], "finished_utc": utc(),
              "source": source_snapshot(manifest), "target_list": [name],
              "unfinished": [], "input_sha256": digest(saved),
              "input_bytes": saved.stat().st_size,
              "results": [{"name": name, "status": status, "steps": steps}]}
    atomic_json(run_dir / "run.json", report)
    print(f"replay: {run_dir}\ninput_sha256: {report['input_sha256']}\n"
          f"status: {status}")
    return 0 if status == "passed" else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    validate = sub.add_parser("validate")
    validate.add_argument("--profile", choices=("fast", "extended", "nightly"))
    sub.add_parser("list")
    run = sub.add_parser("run")
    run.add_argument("--profile", choices=("fast", "extended", "nightly"),
                     required=True)
    run.add_argument("--target", action="append", default=[])
    run.add_argument("--resume", type=Path)
    recover = sub.add_parser("recover-stale")
    recover.add_argument("--run", type=Path, required=True)
    replay = sub.add_parser("replay")
    replay.add_argument("--target", required=True)
    replay.add_argument("--input", type=Path, required=True)
    report = sub.add_parser("report")
    report.add_argument("--run", type=Path)
    args = parser.parse_args()
    manifest = load_manifest()
    errors = validate_manifest(manifest, check_tools=args.action == "validate",
                               profile=args.profile if args.action == "validate" else None)
    if errors and args.action != "list":
        for error in errors:
            print(error, file=sys.stderr)
        return 2
    if args.action == "validate":
        print(f"manifest valid: {len(manifest['targets'])} targets")
        return 0
    if args.action == "list":
        for target in manifest["targets"]:
            reasons = availability(target)
            print(f"{target['name']}: {','.join(target['profiles'])}; "
                  f"{target['execution']}; "
                  f"{'unavailable: ' + '; '.join(reasons) if reasons else 'available'}")
        return 0
    if args.action == "run":
        return perform(manifest, args.profile, args.target, args.resume)
    if args.action == "recover-stale":
        value = recover_stale(args.run)
        print(f"recovered interrupted run on {value['source']['commit']}; "
              f"unfinished: {', '.join(value['unfinished'])}")
        return 0
    if args.action == "replay":
        return replay_one(manifest, args.target, args.input)
    if args.action == "report":
        candidates = sorted(RUNS.glob("*/run.json"))
        if args.run:
            path = args.run / "run.json"
        else:
            path = next((candidate for candidate in reversed(candidates)
                         if json.loads(candidate.read_text()).get("profile") != "replay"),
                        None)
        if path is None or not path.is_file():
            print("no completed verification run", file=sys.stderr)
            return 2
        value = json.loads(path.read_text())
        dirty = len(value["source"].get("dirty_paths", []))
        print(f"{path.parent}: {value['status']} on {value['source']['commit']} "
              f"(dirty paths: {dirty})")
        for item in value["results"]:
            metrics = item.get("fuzz_metrics", {})
            print(f"  {item['name']}: {item['status']} "
                  f"executions={metrics.get('executions', '-')}")
        if value["unfinished"]:
            print("unfinished:", ", ".join(value["unfinished"]))
        return 0 if value["status"] == "passed" and not value["unfinished"] else 1
    raise AssertionError(args.action)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"verification infrastructure error: {error}", file=sys.stderr)
        sys.exit(2)
