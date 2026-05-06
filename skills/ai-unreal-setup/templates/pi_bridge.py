"""Project-local Unreal Editor Python helpers for Pi automation.

These functions are intentionally small wrappers around Unreal editor APIs. Pi can
call them through tools/unreal/ue_remote.py once the editor is running.
"""

from __future__ import annotations

import json
from typing import Iterable

import unreal


def _actor_subsystem():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _editor_subsystem():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)


def _vector(values: Iterable[float]) -> unreal.Vector:
    x, y, z = values
    return unreal.Vector(float(x), float(y), float(z))


def _rotator(values: Iterable[float]) -> unreal.Rotator:
    pitch, yaw, roll = values
    return unreal.Rotator(float(pitch), float(yaw), float(roll))


def snapshot() -> dict:
    """Return a small snapshot of the currently open editor world."""
    actors = _actor_subsystem().get_all_level_actors()
    selected = _actor_subsystem().get_selected_level_actors()
    world = _editor_subsystem().get_editor_world()
    return {
        "engine_version": unreal.SystemLibrary.get_engine_version(),
        "project_dir": unreal.Paths.project_dir(),
        "world": world.get_path_name() if world else None,
        "actor_count": len(actors),
        "selected_actors": [actor.get_actor_label() for actor in selected],
    }


def print_snapshot() -> None:
    print(json.dumps(snapshot(), indent=2, sort_keys=True))


def find_actor(label_or_name: str):
    """Find a level actor by editor label or object name."""
    for actor in _actor_subsystem().get_all_level_actors():
        if actor.get_actor_label() == label_or_name or actor.get_name() == label_or_name:
            return actor
    raise RuntimeError(f"No actor found with label/name: {label_or_name}")


def move_actor(label_or_name: str, location=None, rotation=None, scale=None) -> dict:
    """Move an actor by label/name, returning its new transform."""
    actor = find_actor(label_or_name)
    with unreal.ScopedEditorTransaction(f"Pi move actor {actor.get_actor_label()}"):
        actor.modify()
        if location is not None:
            actor.set_actor_location(_vector(location), False, True)
        if rotation is not None:
            actor.set_actor_rotation(_rotator(rotation), False)
        if scale is not None:
            actor.set_actor_scale3d(_vector(scale))
    return actor_transform(actor)


def actor_transform(actor) -> dict:
    location = actor.get_actor_location()
    rotation = actor.get_actor_rotation()
    scale = actor.get_actor_scale3d()
    return {
        "label": actor.get_actor_label(),
        "name": actor.get_name(),
        "location": [location.x, location.y, location.z],
        "rotation": [rotation.pitch, rotation.yaw, rotation.roll],
        "scale": [scale.x, scale.y, scale.z],
    }


def save_current_level() -> bool:
    """Save the currently open editor level."""
    return unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
