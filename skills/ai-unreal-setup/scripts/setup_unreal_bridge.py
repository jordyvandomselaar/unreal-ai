#!/usr/bin/env python3
"""Install the Pi/Unreal editor automation bridge into an Unreal project.

This script is intentionally dependency-free. It modifies project config files and
copies template Python/C++ bridge files; it does not install packages, Visual Studio
components, or other system tools.
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path


PYTHON_SETTINGS_SECTION = "[/Script/PythonScriptPlugin.PythonScriptPluginSettings]"
REMOTE_CONTROL_SECTION = "[/Script/RemoteControlCommon.RemoteControlSettings]"

PYTHON_SETTINGS = {
    "bRemoteExecution": "True",
    "RemoteExecutionMulticastGroupEndpoint": "239.0.0.1:6766",
    "RemoteExecutionMulticastBindAddress": "127.0.0.1",
    "RemoteExecutionSendBufferSizeBytes": "2097152",
    "RemoteExecutionReceiveBufferSizeBytes": "2097152",
    "RemoteExecutionMulticastTtl": "0",
    "bRunPipInstallOnStartup": "False",
}

REMOTE_CONTROL_SETTINGS = {
    "bAutoStartWebServer": "True",
    "bAutoStartWebSocketServer": "True",
    "RemoteControlHttpServerPort": "30010",
    "RemoteControlWebSocketServerPort": "30020",
    "RemoteControlWebsocketServerBindAddress": "127.0.0.1",
    "RemoteControlWebInterfaceBindAddress": "127.0.0.1",
    "RemoteControlWebInterfacePort": "30000",
    # Keep the HTTP/WebSocket listeners bound to 127.0.0.1. Some Unreal versions
    # reject hand-written AllowlistedClients struct values in config, so this
    # setup relies on loopback-only bind addresses for local safety.
    "bRestrictServerAccess": "False",
    "bEnableRemotePythonExecution": "False",
    "bAllowConsoleCommandRemoteExecution": "False",
    "AllowedOrigin": "*",
    "bEnforcePassphraseForRemoteClients": "True",
}

def find_project_dir(start: Path) -> Path:
    start = start.resolve()
    for candidate in [start, *start.parents]:
        if list(candidate.glob("*.uproject")):
            return candidate
    raise RuntimeError(f"No .uproject found in or above {start}")


def load_uproject(project_dir: Path) -> tuple[Path, dict]:
    matches = sorted(project_dir.glob("*.uproject"))
    if len(matches) != 1:
        raise RuntimeError(f"Expected exactly one .uproject in {project_dir}, found {len(matches)}")
    path = matches[0]
    with path.open("r", encoding="utf-8") as fh:
        return path, json.load(fh)


def ensure_plugins(uproject: dict) -> None:
    required = {
        "PythonScriptPlugin": {"Enabled": True, "TargetAllowList": ["Editor"]},
        "EditorScriptingUtilities": {"Enabled": True, "TargetAllowList": ["Editor"]},
        "RemoteControl": {"Enabled": True},
        "PiBlueprintBridge": {"Enabled": True},
    }

    plugins = uproject.setdefault("Plugins", [])
    by_name = {plugin.get("Name"): plugin for plugin in plugins if isinstance(plugin, dict)}

    for name, values in required.items():
        plugin = by_name.get(name)
        if plugin is None:
            plugin = {"Name": name}
            plugins.append(plugin)
        plugin.update(values)


def write_uproject(path: Path, uproject: dict, dry_run: bool) -> None:
    if dry_run:
        print(f"Would write {path}")
        return
    path.write_text(json.dumps(uproject, indent="\t") + "\n", encoding="utf-8")


def _line_key(line: str) -> str | None:
    stripped = line.strip()
    if not stripped or stripped.startswith(";") or stripped.startswith("["):
        return None
    for prefix in ("+", "-", "!"):
        if stripped.startswith(prefix):
            stripped = stripped[1:]
            break
    if "=" not in stripped:
        return None
    return stripped.split("=", 1)[0].strip()


def upsert_ini_section(
    path: Path,
    section: str,
    settings: dict[str, str],
    *,
    extra_lines: list[str] | None = None,
    remove_keys: set[str] | None = None,
    dry_run: bool,
) -> None:
    extra_lines = extra_lines or []
    remove_keys = remove_keys or set()

    text = path.read_text(encoding="utf-8") if path.exists() else ""
    lines = text.splitlines()
    output: list[str] = []
    found_section = False
    in_section = False
    pending = dict(settings)
    emitted_extra = False

    def emit_pending() -> None:
        nonlocal emitted_extra
        for key, value in pending.items():
            output.append(f"{key}={value}")
        pending.clear()
        if not emitted_extra:
            output.extend(extra_lines)
            emitted_extra = True

    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            if in_section:
                emit_pending()
                output.append("")
            in_section = stripped == section
            found_section = found_section or in_section
            output.append(line)
            continue

        if in_section:
            key = _line_key(line)
            if key in remove_keys:
                continue
            if key in pending:
                output.append(f"{key}={pending.pop(key)}")
                continue
        output.append(line)

    if in_section:
        emit_pending()

    if not found_section:
        if output and output[-1].strip():
            output.append("")
        output.append(section)
        for key, value in settings.items():
            output.append(f"{key}={value}")
        output.extend(extra_lines)

    new_text = "\n".join(output).rstrip() + "\n"
    if dry_run:
        print(f"Would write {path}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(new_text, encoding="utf-8")


def copy_templates(skill_dir: Path, project_dir: Path, dry_run: bool) -> None:
    templates = skill_dir / "templates"
    destinations = {
        templates / "ue_remote.py": project_dir / "tools" / "unreal" / "ue_remote.py",
        templates / "ue_remote.cmd": project_dir / "tools" / "unreal" / "ue_remote.cmd",
        templates / "README.md": project_dir / "tools" / "unreal" / "README.md",
        templates / "pi_bridge.py": project_dir / "Content" / "Python" / "pi_bridge.py",
    }

    for source, destination in destinations.items():
        if dry_run:
            print(f"Would copy {source} -> {destination}")
            continue
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)

    plugin_source = templates / "PiBlueprintBridge"
    plugin_destination = project_dir / "Plugins" / "PiBlueprintBridge"
    if dry_run:
        print(f"Would copy {plugin_source} -> {plugin_destination}")
    else:
        shutil.copytree(plugin_source, plugin_destination, dirs_exist_ok=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Install Pi Unreal editor automation bridge files.")
    parser.add_argument("project_dir", nargs="?", default=".", help="Unreal project directory or any child path.")
    parser.add_argument("--dry-run", action="store_true", help="Print intended writes without modifying files.")
    args = parser.parse_args(argv)

    skill_dir = Path(__file__).resolve().parents[1]
    project_dir = find_project_dir(Path(args.project_dir))
    uproject_path, uproject = load_uproject(project_dir)
    project_name = uproject_path.stem

    ensure_plugins(uproject)
    write_uproject(uproject_path, uproject, args.dry_run)

    config_dir = project_dir / "Config"
    upsert_ini_section(
        config_dir / "DefaultEngine.ini",
        PYTHON_SETTINGS_SECTION,
        PYTHON_SETTINGS,
        dry_run=args.dry_run,
    )
    upsert_ini_section(
        config_dir / "DefaultRemoteControl.ini",
        REMOTE_CONTROL_SECTION,
        REMOTE_CONTROL_SETTINGS,
        remove_keys={"AllowlistedClients"},
        dry_run=args.dry_run,
    )
    copy_templates(skill_dir, project_dir, args.dry_run)

    print(f"Pi Unreal bridge {'dry-run complete' if args.dry_run else 'installed'} for {project_name} at {project_dir}")
    print("Human step: save/restart Unreal Editor before verification.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"setup_unreal_bridge.py: {exc}", file=sys.stderr)
        raise SystemExit(1)
