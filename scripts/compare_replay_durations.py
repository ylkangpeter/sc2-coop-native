# -*- coding: utf-8 -*-
"""Compare estimated match ends with replay exits without changing any replay or database."""

import argparse
import csv
import json
import math
import os
import statistics
from collections import Counter
from concurrent.futures import ProcessPoolExecutor
from functools import lru_cache
from pathlib import Path

try:
    from mpyq import MPQArchive
    from s2protocol import versions
except ImportError as error:
    raise SystemExit("Install the analysis dependencies with python -m pip install s2protocol mpyq") from error


LOOPS_PER_GAME_SECOND = 16
VICTORY_ACHIEVEMENTS = {693, 694, 695, 696}
ARCADE_OBJECTIVES = {
    "[mm] part and parcel": ({b"HybridDominatorCoopBoss"}, 21, 25),
    "[mm] oblivion express": ({b"TarsonisEngine", b"TarsonisEngineFast"}, 17, 35),
}
BUILDS = tuple(int(name[8:-3]) for name in versions.list_all())


@lru_cache(maxsize=None)
def protocol(build):
    return versions.build(build)


def decode(archive, stream, build, final_loop, summarize):
    data = archive.read_file(f"replay.{stream}.events")
    if data is None:
        raise ValueError(f"missing {stream} event stream")
    failures = []
    # Some regional clients use base builds between published protocol schemas.
    for candidate in sorted(BUILDS, key=lambda value: (abs(value - build), value))[:4]:
        try:
            decoder = getattr(protocol(candidate), f"decode_replay_{stream}_events")
            return summarize(decoder(data), final_loop), candidate
        except Exception as error:
            failures.append(f"{candidate}: {type(error).__name__}: {error}")
    raise ValueError(f"cannot decode {stream}: {'; '.join(failures)}")


def game_signals(events, final_loop):
    exits = []
    dialogue = []
    achievements = {}
    last_order = None
    for event in events:
        tick = event["_gameloop"]
        if tick > final_loop + LOOPS_PER_GAME_SECOND:
            raise ValueError(f"game event at {tick} is beyond replay end {final_loop}")
        kind = event["_event"].rsplit(".", 1)[-1]
        if kind == "SGameUserLeaveEvent":
            exits.append(tick)
        elif kind == "STriggerTransmissionCompleteEvent":
            dialogue.append(tick)
        elif kind == "SCmdEvent":
            last_order = tick
        elif kind == "SAchievementAwardedEvent" and event["m_achievementLink"] in VICTORY_ACHIEVEMENTS:
            # Group IDs per tick. The same bundle can be awarded to both players.
            achievements.setdefault(tick, set()).add(event["m_achievementLink"])
    return exits, dialogue, achievements, last_order


def objective_deaths(events, final_loop, targets):
    units = {}
    deaths = []
    for event in events:
        tick = event["_gameloop"]
        if tick > final_loop + LOOPS_PER_GAME_SECOND:
            raise ValueError(f"tracker event at {tick} is beyond replay end {final_loop}")
        if "m_unitTagIndex" not in event or "m_unitTagRecycle" not in event:
            continue
        tag = (event["m_unitTagIndex"], event["m_unitTagRecycle"])
        if "m_unitTypeName" in event:
            units[tag] = event["m_unitTypeName"]
        if event["_event"].endswith(".SUnitDiedEvent") and units.pop(tag, None) in targets:
            deaths.append(tick)
    return deaths


def estimate(title, result, dialogue, achievements, last_order, deaths):
    end_dialogue = dialogue[-1] if dialogue else None
    bundles = [tick for tick, ids in achievements.items() if len(ids) >= 3]
    if not title.casefold().startswith("[mm]") and result == "Win" and bundles:
        for tick in bundles:
            if any(0 <= tick - line <= 3 * LOOPS_PER_GAME_SECOND for line in reversed(dialogue)):
                return tick, "victory_achievement_and_dialogue", "high"
        return bundles[0], "victory_achievement", "medium"

    for prefix, (_, count, delay) in ARCADE_OBJECTIVES.items():
        if title.casefold().startswith(prefix) and len(deaths) == count and end_dialogue is not None:
            last_target = deaths[-1]
            if 0 <= end_dialogue - last_target <= delay * LOOPS_PER_GAME_SECOND:
                return last_target, "arcade_objective_and_dialogue", "medium"

    # A known win can use the end of the last mission line when play and dialogue
    # stopped together. Leave unconfirmed results and unsupported maps as fallbacks.
    if result == "Win" and end_dialogue is not None and last_order is not None:
        gap = (end_dialogue - last_order) / LOOPS_PER_GAME_SECOND
        if end_dialogue >= 120 * LOOPS_PER_GAME_SECOND and -5 <= gap <= 25:
            return end_dialogue, "victory_dialogue_near_last_order", "low"
    return None, "recorded_duration_fallback", "unresolved"


def seconds(tick):
    return round(tick / LOOPS_PER_GAME_SECOND, 3) if tick is not None else None


FIELDS = (
    "file", "title", "replay_result", "base_build", "game_protocol", "tracker_protocol",
    "recorded_metadata_s", "replay_header_s", "first_user_leave_s", "last_user_leave_s",
    "estimated_match_end_s", "estimate_method", "confidence", "exit_minus_estimate_s",
    "first_leave_after_estimate_s", "first_leave_after_estimate_minus_s",
    "header_minus_estimate_s", "last_order_s", "last_dialogue_s", "objective_deaths", "notes",
)


def analyze(filename):
    row = dict.fromkeys(FIELDS)
    row["file"] = filename
    try:
        with open(filename, "rb") as handle:
            archive = MPQArchive(handle)
            header = protocol(max(BUILDS)).decode_replay_header(archive.header["user_data_header"]["content"])
            final_loop = header["m_elapsedGameLoops"]
            build = header["m_version"]["m_baseBuild"]
            row.update(base_build=build, replay_header_s=seconds(final_loop))
            metadata = json.loads(archive.read_file("replay.gamemetadata.json"))
            title = metadata["Title"]
            results = {player.get("Result") for player in metadata.get("Players", [])}
            result = "Win" if "Win" in results else "Loss" if "Loss" in results else "Undecided"
            row.update(title=title, replay_result=result, recorded_metadata_s=metadata.get("Duration"))
            signals, used = decode(archive, "game", build, final_loop, game_signals)
            exits, dialogue, achievements, last_order = signals
            row.update(game_protocol=used, first_user_leave_s=seconds(exits[0]) if exits else None,
                       last_user_leave_s=seconds(exits[-1]) if exits else None,
                       last_order_s=seconds(last_order), last_dialogue_s=seconds(dialogue[-1]) if dialogue else None)

            deaths = []
            for prefix, (targets, _, _) in ARCADE_OBJECTIVES.items():
                if title.casefold().startswith(prefix):
                    try:
                        deaths, used = decode(archive, "tracker", build, final_loop,
                                              lambda events, end: objective_deaths(events, end, targets))
                        row.update(tracker_protocol=used, objective_deaths=len(deaths))
                    except ValueError as error:
                        row["notes"] = str(error)
                    break

            tick, method, confidence = estimate(title, result, dialogue, achievements, last_order, deaths)
            if tick is None:
                tick = final_loop
            elif tick > final_loop or (exits and tick > exits[-1] + LOOPS_PER_GAME_SECOND):
                row["notes"] = "candidate estimate falls after replay or last user exit"
                tick, method, confidence = final_loop, "recorded_duration_fallback", "unresolved"
            first_exit_after = next((exit_tick for exit_tick in exits if exit_tick >= tick), None)
            row.update(estimated_match_end_s=seconds(tick), estimate_method=method, confidence=confidence,
                       exit_minus_estimate_s=seconds(exits[-1] - tick) if exits else None,
                       first_leave_after_estimate_s=seconds(first_exit_after) if confidence != "unresolved" else None,
                       first_leave_after_estimate_minus_s=seconds(first_exit_after - tick)
                       if first_exit_after is not None and confidence != "unresolved" else None,
                       header_minus_estimate_s=seconds(final_loop - tick))
    except Exception as error:
        row.update(estimate_method="decode_error", confidence="unresolved", notes=f"{type(error).__name__}: {error}")
    return row


def distribution(rows, field):
    values = sorted(row[field] for row in rows if row[field] is not None)
    return describe(values)


def describe(values):
    if not values:
        return {"count": 0}
    return {
        "count": len(values), "median_s": round(statistics.median(values), 3),
        "p95_s": values[math.ceil(0.95 * len(values)) - 1], "max_s": values[-1],
        "estimate_after_baseline": sum(value < 0 for value in values),
        "within_5s": sum(0 <= value <= 5 for value in values),
        **{f"over_{threshold}s": sum(value > threshold for value in values) for threshold in (30, 60, 300, 600, 3600)},
    }


def summarize(rows):
    estimates = [row for row in rows if row["confidence"] != "unresolved"]
    methods = {}
    for method, count in sorted(Counter(row["estimate_method"] for row in rows).items()):
        group = [row for row in estimates if row["estimate_method"] == method]
        methods[method] = {"count": count, "vs_exit": distribution(group, "exit_minus_estimate_s"),
                           "vs_first_exit_after_estimate": distribution(group, "first_leave_after_estimate_minus_s"),
                           "vs_header": distribution(group, "header_minus_estimate_s")}
    outliers = sorted(estimates, key=lambda row: row["header_minus_estimate_s"], reverse=True)[:25]
    unresolved_loss = [row for row in rows if row["confidence"] == "unresolved" and row["replay_result"] == "Loss"]
    loss_proxies = {}
    for field in ("last_order_s", "last_dialogue_s"):
        deltas = sorted(round(row["last_user_leave_s"] - row[field], 3)
                        for row in unresolved_loss if row["last_user_leave_s"] is not None and row[field] is not None)
        loss_proxies[field] = describe(deltas)
    return {
        "files": len(rows), "estimates": len(estimates), "confidence_counts": Counter(row["confidence"] for row in rows),
        "result_counts": Counter(row["replay_result"] or "unknown" for row in rows),
        "confidence_by_result": {result: Counter(row["confidence"] for row in rows if row["replay_result"] == result)
                                 for result in ("Win", "Loss", "Undecided", None)},
        "without_exit_event": sum(row["last_user_leave_s"] is None for row in rows),
        "game_protocol_fallbacks": sum(row["game_protocol"] is not None and row["game_protocol"] != row["base_build"] for row in rows),
        "vs_exit": distribution(estimates, "exit_minus_estimate_s"),
        "vs_first_exit_after_estimate": distribution(estimates, "first_leave_after_estimate_minus_s"),
        "vs_header": distribution(estimates, "header_minus_estimate_s"),
        "unresolved_loss_proxies_vs_exit": loss_proxies,
        "methods": methods, "largest_corrections": outliers,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("replay_root", type=Path, help="directory to scan recursively, usually StarCraft II/Accounts")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=min(12, os.cpu_count() or 1))
    args = parser.parse_args()
    if args.workers < 1 or not args.replay_root.is_dir():
        parser.error("a valid replay directory and a positive worker count are required")
    files = sorted(str(path) for path in args.replay_root.resolve().rglob("*")
                   if path.suffix.lower() == ".sc2replay" and path.is_file())
    if not files:
        parser.error("no .SC2Replay files found")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    csv_path = args.output_dir / "replay_durations.csv"
    with csv_path.open("w", encoding="utf-8-sig", newline="") as output, ProcessPoolExecutor(max_workers=args.workers) as pool:
        writer = csv.DictWriter(output, fieldnames=FIELDS)
        writer.writeheader()
        rows = []
        print(f"Scanning {len(files)} replays using {args.workers} processes", flush=True)
        for index, row in enumerate(pool.map(analyze, files, chunksize=4), 1):
            writer.writerow(row)
            rows.append(row)
            if index % 100 == 0 or index == len(files):
                output.flush()
                print(f"{index}/{len(files)}", flush=True)

    summary_path = args.output_dir / "summary.json"
    summary_path.write_text(json.dumps(summarize(rows), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Comparison: {csv_path}\nSummary: {summary_path}", flush=True)


if __name__ == "__main__":
    main()
