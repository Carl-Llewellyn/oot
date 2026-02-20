#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
import struct
from pathlib import Path

from dmadata import read_dmadata


ENTRY_RE = re.compile(r'^\s*DEFINE_DMA_ENTRY\(([^,]+),\s*"([^"]+)"\)')
OBJ_RE = re.compile(r"/\* 0x([0-9A-Fa-f]+) \*/\s+DEFINE_OBJECT\(([^,]+),\s*([^,)]+)")
ACT_RE = re.compile(r"/\* 0x([0-9A-Fa-f]+) \*/\s+DEFINE_ACTOR\(([^,]+),\s*([^,]+),")
ACT_INT_RE = re.compile(r"/\* 0x([0-9A-Fa-f]+) \*/\s+DEFINE_ACTOR_INTERNAL\(([^,]+),\s*([^,]+),")
GI_RE = re.compile(r"\s*/\* 0x([0-9A-Fa-f]+) \*/\s*(GI_[A-Z0-9_]+),")
GET_ITEM_RE = re.compile(r"GET_ITEM\(([^,]+),\s*([^,]+),")
SKYBOX_ENUM_RE = re.compile(r"/\*\s*0x([0-9A-Fa-f]+)\s*\*/\s*(SKYBOX_[A-Z0-9_]+)(?:\s*=\s*([0-9]+))?,")
SKYBOX_CASE_RE = re.compile(r"^\s*case\s+(SKYBOX_[A-Z0-9_]+)\s*:")
VR_SEG_REF_RE = re.compile(r"_((?:vr_[A-Za-z0-9]+_(?:pal_)?static))SegmentRomStart")
ROM_FILE_RE = re.compile(r"ROM_FILE\((vr_[A-Za-z0-9]+_(?:pal_)?static)\)")
SCENE_TITLE_RE = re.compile(
    r"DEFINE_SCENE\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*,"
)


def parse_dmadata_spec(spec_path: Path) -> list[str]:
    names: list[str] = []
    for line in spec_path.read_text(encoding="utf-8").splitlines():
        m = ENTRY_RE.match(line)
        if m:
            names.append(m.group(2))
    return names


def parse_object_table(path: Path):
    id_to_seg: dict[int, str] = {}
    enum_to_seg: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        m = OBJ_RE.search(line)
        if not m:
            continue
        idx = int(m.group(1), 16)
        seg = m.group(2)
        enum_name = m.group(3)
        id_to_seg[idx] = seg
        enum_to_seg[enum_name] = seg
    return id_to_seg, enum_to_seg


def parse_actor_table(path: Path):
    id_to_overlay: dict[int, str | None] = {}
    id_to_symbol: dict[int, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        m = ACT_RE.search(line)
        if m:
            idx = int(m.group(1), 16)
            id_to_overlay[idx] = f"ovl_{m.group(2)}"
            id_to_symbol[idx] = m.group(3)
            continue
        m = ACT_INT_RE.search(line)
        if m:
            idx = int(m.group(1), 16)
            id_to_overlay[idx] = None
            id_to_symbol[idx] = m.group(3)
    return id_to_overlay, id_to_symbol


def parse_skybox_enums(path: Path) -> dict[str, int]:
    enum_to_id: dict[str, int] = {}
    next_val = 0
    for line in path.read_text(encoding="utf-8").splitlines():
        m = SKYBOX_ENUM_RE.search(line)
        if not m:
            continue
        name = m.group(2)
        explicit = m.group(3)
        if explicit is not None:
            val = int(explicit, 10)
        else:
            val = next_val
        enum_to_id[name] = val
        next_val = val + 1
    return enum_to_id


def parse_normal_sky_files(path: Path) -> list[str]:
    segs: list[str] = []
    in_table = False
    for line in path.read_text(encoding="utf-8").splitlines():
        if not in_table and "SkyboxFile gNormalSkyFiles[]" in line:
            in_table = True
            continue
        if in_table and line.strip().startswith("};"):
            break
        if in_table:
            m = ROM_FILE_RE.search(line)
            if m:
                segs.append(m.group(1))
    return segs


def parse_skybox_to_segments(
    skybox_h: Path, z_vr_box_c: Path, z_kankyo_c: Path
) -> dict[int, set[str]]:
    enum_to_id = parse_skybox_enums(skybox_h)
    out: dict[int, set[str]] = {}

    current_id: int | None = None
    for line in z_vr_box_c.read_text(encoding="utf-8").splitlines():
        m = SKYBOX_CASE_RE.match(line)
        if m:
            current_id = enum_to_id.get(m.group(1))
            continue
        if current_id is None:
            continue
        for mm in VR_SEG_REF_RE.finditer(line):
            out.setdefault(current_id, set()).add(mm.group(1))

    normal_id = enum_to_id.get("SKYBOX_NORMAL_SKY")
    if normal_id is not None:
        out.setdefault(normal_id, set()).update(parse_normal_sky_files(z_kankyo_c))

    return out


def parse_scene_title_cards(path: Path) -> dict[str, str]:
    scene_to_title: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        m = SCENE_TITLE_RE.search(line)
        if not m:
            continue
        scene_seg = m.group(1)
        title_seg = m.group(2)
        if title_seg != "none":
            scene_to_title[scene_seg] = title_seg
    return scene_to_title


def parse_gi_names(path: Path) -> dict[int, str]:
    gi: dict[int, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        m = GI_RE.match(line)
        if m:
            gi[int(m.group(1), 16)] = m.group(2)
    return gi


def parse_gi_to_object_enum(z_player_path: Path) -> dict[int, str | None]:
    rows: list[str | None] = []
    for line in z_player_path.read_text(encoding="utf-8").splitlines():
        s = line.strip()
        m = GET_ITEM_RE.match(s)
        if m:
            rows.append(m.group(2))
        elif s.startswith("GET_ITEM_NONE"):
            rows.append(None)

    # Table order starts at GI 0x01.
    gi_to_obj: dict[int, str | None] = {0: None}
    for gi_id, obj_enum in enumerate(rows, start=1):
        gi_to_obj[gi_id] = obj_enum
    return gi_to_obj


def seg_ptr_to_off(ptr: int, seg_size: int) -> int | None:
    seg = (ptr >> 24) & 0xFF
    off = ptr & 0x00FFFFFF
    if seg in (0x02, 0x03):
        return off if off < seg_size else None
    if ptr < seg_size:
        return ptr
    return None


def header_looks_valid(data: memoryview, off: int, seg_size: int) -> bool:
    if off < 0 or off + 8 > seg_size:
        return False
    seen = 0
    cur = off
    for _ in range(128):
        if cur + 8 > seg_size:
            return False
        code = data[cur]
        if code > 0x19:
            return False
        seen += 1
        if code == 0x14:
            return seen >= 2
        cur += 8
    return False


def extract_room_segments(
    scene_name: str,
    entry_by_name: dict[str, tuple[int, int]],
    dmadata_entries,
    dmadata_names: list[str],
    rom: memoryview,
) -> list[str]:
    out: list[str] = []
    if scene_name not in entry_by_name:
        return out
    start, size = entry_by_name[scene_name]
    data = rom[start : start + size]
    off = 0
    for _ in range(128):
        if off + 8 > size:
            break
        code = data[off]
        length = data[off + 1]
        d2 = struct.unpack_from(">I", data, off + 4)[0]
        if code == 0x14:
            break
        if code == 0x04:  # room list
            room_list_off = seg_ptr_to_off(d2, size)
            if room_list_off is None:
                break
            for i in range(length):
                rf_off = room_list_off + i * 8
                if rf_off + 8 > size:
                    break
                room_rom_start = struct.unpack_from(">I", data, rf_off)[0]
                # Find matching dmadata entry by rom_start.
                for idx, ent in enumerate(dmadata_entries):
                    if ent.rom_start == room_rom_start:
                        out.append(dmadata_names[idx])
                        break
            break
        off += 8
    return out


def parse_headers_for_deps(
    seg_name: str,
    seg_data: memoryview,
    object_ids: set[int],
    actor_ids: set[int],
    chest_items: set[tuple[str, int, int]],
    skybox_ids: set[int],
) -> int:
    seg_size = len(seg_data)
    visited_headers: set[int] = set()
    stack = [0]

    while stack:
        hdr_off = stack.pop()
        if hdr_off in visited_headers or not header_looks_valid(seg_data, hdr_off, seg_size):
            continue
        visited_headers.add(hdr_off)
        off = hdr_off

        for _ in range(128):
            code = seg_data[off]
            length = seg_data[off + 1]
            d2 = struct.unpack_from(">I", seg_data, off + 4)[0]

            if code == 0x14:  # END
                break

            if code == 0x0B:  # OBJECT_LIST
                list_off = seg_ptr_to_off(d2, seg_size)
                if list_off is not None and list_off + (2 * length) <= seg_size:
                    for i in range(length):
                        oid = struct.unpack_from(">H", seg_data, list_off + 2 * i)[0]
                        object_ids.add(oid)

            elif code == 0x01:  # ACTOR_LIST
                list_off = seg_ptr_to_off(d2, seg_size)
                if list_off is not None and list_off + (16 * length) <= seg_size:
                    for i in range(length):
                        base = list_off + 16 * i
                        aid = struct.unpack_from(">H", seg_data, base)[0]
                        params = struct.unpack_from(">H", seg_data, base + 14)[0]
                        actor_ids.add(aid)
                        if aid == 0x000A:  # En_Box
                            chest_items.add((seg_name, (params >> 5) & 0x7F, params))

            elif code == 0x0E:  # TRANSITION_ACTOR_LIST
                list_off = seg_ptr_to_off(d2, seg_size)
                if list_off is not None and list_off + (16 * length) <= seg_size:
                    for i in range(length):
                        aid = struct.unpack_from(">H", seg_data, list_off + 16 * i + 4)[0]
                        actor_ids.add(aid)
            elif code == 0x07:  # SPECIAL_FILES
                keep_object_id = d2 & 0xFFFF
                if keep_object_id != 0:
                    object_ids.add(keep_object_id)

            elif code == 0x18:  # ALTERNATE_HEADER_LIST
                list_off = seg_ptr_to_off(d2, seg_size)
                if list_off is not None:
                    zero_run = 0
                    for i in range(32):
                        p_off = list_off + 4 * i
                        if p_off + 4 > seg_size:
                            break
                        p = struct.unpack_from(">I", seg_data, p_off)[0]
                        alt = seg_ptr_to_off(p, seg_size)
                        if alt is None:
                            zero_run += 1
                            if zero_run >= 8:
                                break
                            continue
                        zero_run = 0
                        if alt not in visited_headers and header_looks_valid(seg_data, alt, seg_size):
                            stack.append(alt)
            elif code == 0x11:  # SKYBOX_SETTINGS
                skybox_ids.add(seg_data[off + 4])

            off += 8

    return len(visited_headers)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", default="ntsc-1.2")
    parser.add_argument("--dmadata-start", default="0x7960")
    parser.add_argument(
        "--segments",
        required=True,
        help="comma-separated dmadata segment names (scene/room list seed)",
    )
    parser.add_argument(
        "--emit-relocate-names",
        help="optional output file path for comma-separated relocate names",
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    dmadata_start = int(args.dmadata_start, 0)

    rom_path = root / f"build/{args.version}/oot-{args.version}.z64"
    spec_path = root / f"build/{args.version}/dmadata_table_spec.h"
    if not rom_path.exists() or not spec_path.exists():
        raise SystemExit(f"missing build artifacts under build/{args.version}")

    rom = memoryview(rom_path.read_bytes())
    dmadata_entries = read_dmadata(rom, dmadata_start)
    dmadata_names = parse_dmadata_spec(spec_path)
    if len(dmadata_entries) < len(dmadata_names):
        raise SystemExit("dmadata/spec length mismatch")

    entry_by_name: dict[str, tuple[int, int]] = {}
    for i, name in enumerate(dmadata_names):
        ent = dmadata_entries[i]
        entry_by_name[name] = (ent.rom_start, ent.vrom_end - ent.vrom_start)

    object_id_to_seg, object_enum_to_seg = parse_object_table(root / "include/tables/object_table.h")
    actor_id_to_overlay, actor_id_to_symbol = parse_actor_table(root / "include/tables/actor_table.h")
    gi_id_to_name = parse_gi_names(root / "include/item.h")
    gi_id_to_object_enum = parse_gi_to_object_enum(root / "src/overlays/actors/ovl_player_actor/z_player.c")
    skybox_id_to_segments = parse_skybox_to_segments(
        root / "include/skybox.h",
        root / "src/code/z_vr_box.c",
        root / "src/code/z_kankyo.c",
    )
    scene_to_title_card = parse_scene_title_cards(root / "include/tables/scene_table.h")

    requested = [s.strip() for s in args.segments.split(",") if s.strip()]
    expanded = set(requested)
    # Auto-expand scene room lists.
    for seg in list(requested):
        if not seg.endswith("_scene"):
            continue
        for room in extract_room_segments(seg, entry_by_name, dmadata_entries, dmadata_names, rom):
            expanded.add(room)

    object_ids: set[int] = set()
    actor_ids: set[int] = set()
    chest_items: set[tuple[str, int, int]] = set()
    skybox_ids: set[int] = set()
    headers_total = 0

    for seg in sorted(expanded):
        entry = entry_by_name.get(seg)
        if entry is None:
            continue
        start, size = entry
        seg_data = rom[start : start + size]
        headers_total += parse_headers_for_deps(
            seg, seg_data, object_ids, actor_ids, chest_items, skybox_ids
        )

    object_segments = sorted(
        {
            object_id_to_seg[oid]
            for oid in object_ids
            if oid in object_id_to_seg
        }
    )
    actor_overlays = sorted(
        {
            actor_id_to_overlay[aid]
            for aid in actor_ids
            if aid in actor_id_to_overlay and actor_id_to_overlay[aid] is not None
        }
    )
    internal_actors = sorted(
        {
            actor_id_to_symbol[aid]
            for aid in actor_ids
            if aid in actor_id_to_overlay and actor_id_to_overlay[aid] is None
        }
    )

    chest_items_sorted = sorted(chest_items)
    chest_gi_ids = sorted({gi for _, gi, _ in chest_items_sorted})
    chest_gi_objects = []
    for gi in chest_gi_ids:
        obj_enum = gi_id_to_object_enum.get(gi)
        obj_seg = object_enum_to_seg.get(obj_enum, None) if obj_enum else None
        chest_gi_objects.append((gi, gi_id_to_name.get(gi, f"GI_0x{gi:02X}"), obj_enum, obj_seg))

    skybox_segments = sorted(
        {
            seg
            for skybox_id in skybox_ids
            for seg in skybox_id_to_segments.get(skybox_id, set())
        }
    )
    title_card_segments = sorted(
        {
            scene_to_title_card[seg]
            for seg in expanded
            if seg.endswith("_scene") and seg in scene_to_title_card
        }
    )

    # Preserve a deterministic priority order:
    # 1) seed scenes/rooms in user-specified order (+auto-expanded scene rooms),
    # 2) direct dependencies sorted by class.
    expanded_ordered: list[str] = []
    seen_expanded: set[str] = set()
    for seg in requested:
        if seg in expanded and seg not in seen_expanded:
            seen_expanded.add(seg)
            expanded_ordered.append(seg)
    for seg in sorted(expanded):
        if seg not in seen_expanded:
            seen_expanded.add(seg)
            expanded_ordered.append(seg)

    relocate_names: list[str] = []
    seen_relocate: set[str] = set()

    def add_name(name: str | None) -> None:
        if name is None or name in seen_relocate:
            return
        seen_relocate.add(name)
        relocate_names.append(name)

    for seg in expanded_ordered:
        add_name(seg)
    for seg in object_segments:
        add_name(seg)
    for seg in actor_overlays:
        add_name(seg)
    for _, _, _, obj_seg in chest_gi_objects:
        add_name(obj_seg)
    for seg in skybox_segments:
        add_name(seg)
    for seg in title_card_segments:
        add_name(seg)

    if args.emit_relocate_names:
        out_path = Path(args.emit_relocate_names)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(",".join(relocate_names), encoding="utf-8")

    print("== Scan Summary ==")
    print(f"Requested segments: {len(requested)}")
    print(f"Expanded segments: {len(expanded)}")
    print(f"Parsed headers: {headers_total}")
    print()

    print("== Expanded Segments ==")
    for s in sorted(expanded):
        print(s)
    print()

    print("== Object Segments From Scene/Room Headers ==")
    for s in object_segments:
        print(s)
    print()

    print("== Actor Overlays From Scene/Room Headers ==")
    for s in actor_overlays:
        print(s)
    print()

    print("== Internal Actors (No Overlay) ==")
    for s in internal_actors:
        print(s)
    print()

    print("== Chest GetItem IDs (From En_Box Params) ==")
    for seg, gi, params in chest_items_sorted:
        print(f"{seg}: {gi_id_to_name.get(gi, f'GI_0x{gi:02X}')} params=0x{params:04X}")
    print()

    print("== Chest GetItem -> Object Dependencies ==")
    for gi, gi_name, obj_enum, obj_seg in chest_gi_objects:
        print(f"{gi_name} (0x{gi:02X}) -> {obj_enum or 'N/A'} -> {obj_seg or 'N/A'}")

    print()
    print("== Skybox Segments ==")
    for seg in skybox_segments:
        print(seg)

    print()
    print("== Scene Title Card Segments ==")
    for seg in title_card_segments:
        print(seg)

    print()
    print("== Relocate Names ==")
    print(f"count={len(relocate_names)}")
    for name in relocate_names:
        print(name)


if __name__ == "__main__":
    main()
