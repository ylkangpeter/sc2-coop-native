# -*- coding: utf-8 -*-
"""Generate the native game-event layout table from Blizzard's MIT-licensed s2protocol."""

from functools import lru_cache
from pathlib import Path

from s2protocol import versions


def generate():
    nodes = []
    children = []
    layouts = []
    build_layouts = []

    @lru_cache(maxsize=None)
    def node(kind, bits=0, offset=0, links=()):
        index = len(children)
        children.extend(links)
        nodes.append(f"{{Kind::{kind}, {bits}, {offset}LL, {index}, {len(links)}}}")
        return len(nodes) - 1

    for name in versions.list_all():
        build = int(name[8:-3])
        if build < 51702:
            continue
        source = versions.build(build)

        @lru_cache(maxsize=None)
        def layout_type(type_id):
            kind, args = source.typeinfos[type_id]
            if kind == "_bool":
                return node("Integer", 1)
            if kind in {"_int", "_blob", "_bitarray"}:
                lower, width = args[0]
                return node({"_int": "Integer", "_blob": "Blob", "_bitarray": "BitArray"}[kind], width, lower)
            if kind in {"_fourcc", "_real32", "_real64", "_null"}:
                return node("Integer", {"_fourcc": 32, "_real32": 32, "_real64": 64, "_null": 0}[kind])
            if kind == "_optional":
                return node("Optional", links=((0, layout_type(args[0])),))
            if kind == "_array":
                (lower, width), element = args
                return node("Array", width, lower, ((0, layout_type(element)),))
            if kind == "_struct":
                return node("Struct", links=tuple((0, layout_type(field_type)) for _, field_type, _ in args[0]))
            if kind == "_choice":
                (lower, width), options = args
                return node("Choice", width, lower, tuple((tag, layout_type(field_type))
                            for tag, (_, field_type) in sorted(options.items())))
            raise ValueError(kind)

        observed = {
            "SCmdEvent": "Order", "STriggerTransmissionCompleteEvent": "Dialogue",
            "SAchievementAwardedEvent": "Achievement", "SGameUserLeaveEvent": "Leave",
        }
        events = tuple((code, layout_type(typ), observed.get(event.rsplit(".", 1)[-1], "Other"))
                       for code, (typ, event) in sorted(source.game_event_types.items()))
        key = (layout_type(source.svaruint32_typeid), layout_type(source.replay_userid_typeid),
               layout_type(source.game_eventid_typeid), events)
        if key not in layouts:
            layouts.append(key)
        build_layouts.append((build, layouts.index(key)))

    lines = ["// Generated from Blizzard Entertainment's s2protocol by scripts/generate_game_event_protocol.py.",
             "// License: MIT. See assets/licenses/s2protocol.txt.",
             "constexpr LayoutNode kNodes[] = {"]
    lines.extend(f"    {value}," for value in nodes)
    lines.append("};\nconstexpr LayoutChild kChildren[] = {")
    lines.extend(f"    {{{tag}, {target}}}," for tag, target in children)
    lines.append("};")
    for index, (_, _, _, events) in enumerate(layouts):
        lines.append(f"constexpr EventLayout kEvents{index}[] = {{")
        lines.extend(f"    {{{code}, {typ}, Signal::{label}}}," for code, typ, label in events)
        lines.append("};")
    lines.append("constexpr GameLayout kLayouts[] = {")
    lines.extend(f"    {{{delta}, {user}, {event}, kEvents{index}}},"
                 for index, (delta, user, event, _) in enumerate(layouts))
    lines.append("};\nconstexpr BuildLayout kBuilds[] = {")
    lines.extend(f"    {{{build}, {layout}}}," for build, layout in build_layouts)
    lines.append("};")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    output = Path(__file__).resolve().parents[1] / "src/replay/game_event_protocol.inc"
    output.write_text(generate(), encoding="utf-8", newline="\n")
