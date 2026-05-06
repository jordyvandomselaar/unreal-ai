#!/usr/bin/env python3
r"""Small command-line bridge for Unreal Editor Python remote execution.

Run this with Windows Python when the Unreal Editor is running on Windows, e.g.:

    py -3 tools\unreal\ue_remote.py list
    py -3 tools\unreal\ue_remote.py smoke
    py -3 tools\unreal\ue_remote.py exec "print('hello from unreal')"
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import sys
import time
from pathlib import Path
from typing import Any


def _project_root() -> Path:
    here = Path(__file__).resolve()
    for parent in [here.parent, *here.parents]:
        if any(parent.glob("*.uproject")):
            return parent
    raise RuntimeError(f"Could not find a .uproject above {here}")


def _uproject_path(project_root: Path) -> Path:
    matches = sorted(project_root.glob("*.uproject"))
    if not matches:
        raise RuntimeError(f"No .uproject found in {project_root}")
    return matches[0]


def _project_name(project_root: Path) -> str:
    return _uproject_path(project_root).stem


def _default_engine_root(engine_association: str) -> Path:
    if os.name == "nt":
        return Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Epic Games" / f"UE_{engine_association}"
    return Path("/mnt/c/Program Files/Epic Games") / f"UE_{engine_association}"


def _engine_root(project_root: Path) -> Path:
    env_engine_root = os.environ.get("UE_ENGINE_ROOT")
    if env_engine_root:
        return Path(env_engine_root)

    with _uproject_path(project_root).open("r", encoding="utf-8") as fh:
        project = json.load(fh)

    engine_association = project.get("EngineAssociation")
    if not engine_association:
        raise RuntimeError("The .uproject has no EngineAssociation; set UE_ENGINE_ROOT explicitly.")

    return _default_engine_root(str(engine_association))


def _remote_execution_module(engine_root: Path) -> Any:
    remote_execution_path = (
        engine_root
        / "Engine"
        / "Plugins"
        / "Experimental"
        / "PythonScriptPlugin"
        / "Content"
        / "Python"
        / "remote_execution.py"
    )
    if not remote_execution_path.exists():
        raise RuntimeError(
            "Could not find Unreal's remote_execution.py at "
            f"{remote_execution_path}. Set UE_ENGINE_ROOT if Unreal is installed somewhere else."
        )

    spec = importlib.util.spec_from_file_location("ue_remote_execution", remote_execution_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Could not load {remote_execution_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _remote_config(remote_execution: Any) -> Any:
    config = remote_execution.RemoteExecutionConfig()
    # These must match Config/DefaultEngine.ini. Keep this local-only.
    config.multicast_group_endpoint = ("239.0.0.1", 6766)
    config.multicast_bind_address = "127.0.0.1"
    config.multicast_ttl = 0
    config.command_endpoint = ("127.0.0.1", 6776)
    return config


def _discover(remote_execution: Any, timeout: float) -> list[dict[str, Any]]:
    session = remote_execution.RemoteExecution(_remote_config(remote_execution))
    session.start()
    try:
        deadline = time.time() + timeout
        while time.time() < deadline:
            nodes = session.remote_nodes
            if nodes:
                return nodes
            time.sleep(0.1)
        return session.remote_nodes
    finally:
        session.stop()


def _select_node(nodes: list[dict[str, Any]], project_name: str | None) -> dict[str, Any]:
    if project_name:
        matching = [node for node in nodes if node.get("project_name") == project_name]
        if len(matching) == 1:
            return matching[0]
        if len(matching) > 1:
            raise RuntimeError(f"Multiple Unreal nodes found for project {project_name!r}: {matching}")

    if len(nodes) == 1:
        return nodes[0]

    raise RuntimeError(
        "Could not choose an Unreal node automatically. Use --project, or close extra editor instances. "
        f"Nodes: {json.dumps(nodes, indent=2)}"
    )


def _run_remote(
    remote_execution: Any,
    code: str,
    *,
    mode: str,
    project_name: str | None,
    timeout: float,
    unattended: bool,
) -> dict[str, Any]:
    session = remote_execution.RemoteExecution(_remote_config(remote_execution))
    session.start()
    try:
        deadline = time.time() + timeout
        nodes: list[dict[str, Any]] = []
        while time.time() < deadline:
            nodes = session.remote_nodes
            if nodes:
                break
            time.sleep(0.1)
        if not nodes:
            raise RuntimeError("No Unreal Editor Python remote execution nodes found.")

        node = _select_node(nodes, project_name)
        session.open_command_connection(node["node_id"])
        try:
            result = session.run_command(code, unattended=unattended, exec_mode=mode, raise_on_failure=False)
            result["node"] = node
            return result
        finally:
            session.close_command_connection()
    finally:
        session.stop()


def _smoke_code() -> str:
    return r'''
import json
import unreal

def safe(fn, default=None):
    try:
        return fn()
    except Exception as exc:
        return f"<error: {exc}>"

editor = safe(lambda: unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem))
world = safe(lambda: editor.get_editor_world() if editor else None)
actor_subsystem = safe(lambda: unreal.get_editor_subsystem(unreal.EditorActorSubsystem))

payload = {
    "engine_version": safe(lambda: unreal.SystemLibrary.get_engine_version()),
    "project_dir": safe(lambda: unreal.Paths.project_dir()),
    "world": safe(lambda: world.get_path_name() if world else None),
    "selected_actors": safe(lambda: [actor.get_actor_label() for actor in actor_subsystem.get_selected_level_actors()] if actor_subsystem else []),
    "actor_count": safe(lambda: len(actor_subsystem.get_all_level_actors()) if actor_subsystem else None),
}

print("PI_SMOKE " + json.dumps(payload, sort_keys=True))
'''


def _print_result(result: dict[str, Any], *, json_output: bool) -> int:
    if json_output:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        for entry in result.get("output", []):
            text = entry.get("output")
            if text:
                print(text, end="" if str(text).endswith("\n") else "\n")
        if result.get("result") and result.get("result") != "None":
            print(result["result"], end="" if str(result["result"]).endswith("\n") else "\n")
        if not result.get("success", False):
            print("Remote command failed.", file=sys.stderr)
    return 0 if result.get("success", False) else 1


def main(argv: list[str] | None = None) -> int:
    project_root = _project_root()
    engine_root = _engine_root(project_root)
    remote_execution = _remote_execution_module(engine_root)

    parser = argparse.ArgumentParser(description="Control a running Unreal Editor via Python remote execution.")
    parser.add_argument("--timeout", type=float, default=5.0, help="Seconds to wait for the editor to appear.")
    parser.add_argument(
        "--project",
        default=None,
        help="Project name to target when multiple editors are open. Defaults to the .uproject file name.",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    list_parser = subparsers.add_parser("list", help="List discovered Unreal Editor nodes.")
    list_parser.add_argument("--json", action="store_true", help="Print machine-readable JSON.")

    smoke_parser = subparsers.add_parser("smoke", help="Run a harmless sanity check inside the editor.")
    smoke_parser.add_argument("--json", action="store_true", help="Print the raw remote execution result as JSON.")

    exec_parser = subparsers.add_parser("exec", help="Execute Python code inside the editor.")
    exec_parser.add_argument("--file", type=Path, help="Python file to execute.")
    exec_parser.add_argument(
        "--mode",
        choices=[remote_execution.MODE_EXEC_FILE, remote_execution.MODE_EXEC_STATEMENT, remote_execution.MODE_EVAL_STATEMENT],
        default=remote_execution.MODE_EXEC_FILE,
        help="Unreal Python execution mode.",
    )
    exec_parser.add_argument("--json", action="store_true", help="Print the raw remote execution result as JSON.")
    exec_parser.add_argument("code", nargs=argparse.REMAINDER, help="Inline Python code to execute.")

    args = parser.parse_args(argv)

    if args.command == "list":
        nodes = _discover(remote_execution, args.timeout)
        if args.json:
            print(json.dumps(nodes, indent=2, sort_keys=True))
        else:
            if not nodes:
                print("No Unreal Editor Python remote execution nodes found.")
                return 1
            for node in nodes:
                print(
                    f"{node.get('node_id')} project={node.get('project_name')} "
                    f"engine={node.get('engine_version')} root={node.get('project_root')}"
                )
        return 0

    if args.command == "smoke":
        result = _run_remote(
            remote_execution,
            _smoke_code(),
            mode=remote_execution.MODE_EXEC_FILE,
            project_name=args.project or _project_name(project_root),
            timeout=args.timeout,
            unattended=True,
        )
        return _print_result(result, json_output=args.json)

    if args.command == "exec":
        if args.file:
            code = args.file.read_text(encoding="utf-8")
        else:
            code = " ".join(args.code).strip()
        if not code:
            raise RuntimeError("Provide inline code or --file.")
        result = _run_remote(
            remote_execution,
            code,
            mode=args.mode,
            project_name=args.project or _project_name(project_root),
            timeout=args.timeout,
            unattended=True,
        )
        return _print_result(result, json_output=args.json)

    parser.error(f"Unknown command: {args.command}")
    return 2


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
    except Exception as exc:
        print(f"ue_remote.py: {exc}", file=sys.stderr)
        raise SystemExit(1)
