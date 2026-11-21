#!/usr/bin/env python3
"""
Builds the official RazorEmulations legal open-source PS5 Prospero homebrew &
diagnostic titles into ZArchive (.zar) packages and updates store/catalog.json.
"""

from __future__ import annotations

import hashlib
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ELFOSABI_FREEBSD = 9
ET_DYNEXEC = 0xFE10

DT_NULL = 0
DT_AUXILIARY = 0x7FFFFFFD
DT_OS_NEEDED_MODULE = 0x6100000F
DT_OS_IMPORT_LIB = 0x61000015
DT_OS_SYMTABSZ = 0x6100003F


def sysv_elf_hash(name: bytes) -> int:
    h = 0
    for b in name:
        h = ((h << 4) + b) & 0xFFFFFFFF
        g = h & 0xF0000000
        if g != 0:
            h ^= g >> 24
        h &= ~g & 0xFFFFFFFF
    return h


def find_section(elf: bytearray, shstrtab: bytes, target_name: bytes):
    e_shoff = struct.unpack_from("<Q", elf, 40)[0]
    e_shentsize, e_shnum = struct.unpack_from("<HH", elf, 58)
    for idx in range(e_shnum):
        off = e_shoff + idx * e_shentsize
        sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize = (
            struct.unpack_from("<IIQQQQIIQQ", elf, off)
        )
        end = shstrtab.find(b"\x00", sh_name)
        sec_name = shstrtab[sh_name:end]
        if sec_name == target_name:
            return {
                "index": idx,
                "addr": sh_addr,
                "offset": sh_offset,
                "size": sh_size,
                "entsize": sh_entsize,
            }
    raise RuntimeError(f"Missing ELF section {target_name!r}")


def find_exact_dynstr_offset(dynstr: bytes, symbol: bytes) -> int:
    needle = b"\x00" + symbol + b"\x00"
    pos = dynstr.find(needle)
    if pos < 0:
        raise RuntimeError(f"Anchor symbol {symbol!r} not found in .dynstr")
    return pos + 1


def patch_prospero_elf(elf_path: Path) -> None:
    data = bytearray(elf_path.read_bytes())
    if data[:4] != b"\x7fELF":
        raise RuntimeError("Not an ELF64 binary")

    # Prospero ET_DYNEXEC + FreeBSD OSABI + NextGen ABIVERSION=2
    data[7] = ELFOSABI_FREEBSD
    data[8] = 2
    struct.pack_into("<H", data, 16, ET_DYNEXEC)

    e_shoff = struct.unpack_from("<Q", data, 40)[0]
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", data, 58)
    shstr_hdr_off = e_shoff + e_shstrndx * e_shentsize
    _, _, _, _, shstr_off, shstr_sz, _, _, _, _ = struct.unpack_from("<IIQQQQIIQQ", data, shstr_hdr_off)
    shstrtab = bytes(data[shstr_off : shstr_off + shstr_sz])

    dynstr_sec = find_section(data, shstrtab, b".dynstr")
    dynsym_sec = find_section(data, shstrtab, b".dynsym")
    hash_sec = find_section(data, shstrtab, b".hash")
    dynamic_sec = find_section(data, shstrtab, b".dynamic")

    # 1. Rewrite 15-byte placeholder names "X_Y_<11charNID>" -> "<11charNID>#X#Y" in .dynstr
    dynstr_slice = bytes(data[dynstr_sec["offset"] : dynstr_sec["offset"] + dynstr_sec["size"]])
    pattern = re.compile(rb"([A-D])_([A-D])_([A-Za-z0-9+\-]{11})(?=\x00)")

    def repl(m: re.Match[bytes]) -> bytes:
        lib_id, mod_id, nid = m.group(1), m.group(2), m.group(3)
        out = nid + b"#" + lib_id + b"#" + mod_id
        assert len(out) == 15
        return out

    patched_dynstr, count = pattern.subn(repl, dynstr_slice)
    if count == 0:
        raise RuntimeError("No Prospero NID imports matched in .dynstr")
    assert len(patched_dynstr) == len(dynstr_slice)
    data[dynstr_sec["offset"] : dynstr_sec["offset"] + dynstr_sec["size"]] = patched_dynstr

    # 2. Rebuild SysV .hash table for updated .dynstr names
    nbucket, nchain = struct.unpack_from("<II", data, hash_sec["offset"])
    num_syms = dynsym_sec["size"] // 24
    assert nchain == num_syms

    buckets = [0] * nbucket
    chains = [0] * nchain
    for sym_idx in range(1, num_syms):
        st_name = struct.unpack_from("<I", data, dynsym_sec["offset"] + sym_idx * 24)[0]
        end = patched_dynstr.find(b"\x00", st_name)
        sym_name = patched_dynstr[st_name:end]
        if not sym_name:
            continue
        b = sysv_elf_hash(sym_name) % nbucket
        if buckets[b] == 0:
            buckets[b] = sym_idx
        else:
            cur = buckets[b]
            while chains[cur] != 0:
                cur = chains[cur]
            chains[cur] = sym_idx

    hash_pos = hash_sec["offset"] + 8
    for b_val in buckets:
        struct.pack_into("<I", data, hash_pos, b_val)
        hash_pos += 4
    for c_val in chains:
        struct.pack_into("<I", data, hash_pos, c_val)
        hash_pos += 4

    # 3. Inject Prospero DT_OS_SYMTABSZ, DT_OS_IMPORT_LIB, and DT_OS_NEEDED_MODULE into spare DT_NULL slots
    libs_and_mods = [
        (0, b"libkernel"),
        (1, b"VideoOut"),
        (2, b"Pad"),
        (3, b"UserService"),
    ]
    extra_tags: list[tuple[int, int]] = [(DT_OS_SYMTABSZ, dynsym_sec["size"])]
    for idx_id, name_bytes in libs_and_mods:
        str_off = find_exact_dynstr_offset(patched_dynstr, name_bytes)
        lib_val = (idx_id << 48) | (1 << 32) | str_off
        mod_val = (idx_id << 48) | (1 << 40) | (1 << 32) | str_off
        extra_tags.append((DT_OS_IMPORT_LIB, lib_val))
        extra_tags.append((DT_OS_NEEDED_MODULE, mod_val))

    dyn_count = dynamic_sec["size"] // 16
    slot_indices = []
    for i in range(dyn_count):
        d_tag, _ = struct.unpack_from("<QQ", data, dynamic_sec["offset"] + i * 16)
        if d_tag == DT_AUXILIARY:
            slot_indices.append(i)
    if len(slot_indices) < len(extra_tags):
        raise RuntimeError(f"Insufficient DT_AUXILIARY slots in .dynamic ({len(slot_indices)} < {len(extra_tags)})")

    for j, slot_idx in enumerate(slot_indices):
        if j < len(extra_tags):
            tag, val = extra_tags[j]
            struct.pack_into("<QQ", data, dynamic_sec["offset"] + slot_idx * 16, tag, val)
        else:
            # Keep DT_SONAME (14) pointing to 0 ("") so we never prematurely terminate .dynamic
            struct.pack_into("<QQ", data, dynamic_sec["offset"] + slot_idx * 16, 14, 0)

    elf_path.write_bytes(data)


def write_png_rgb(path: Path, width: int, height: int, rows_rgb: list[bytes]) -> None:
    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    raw = bytearray()
    for row in rows_rgb:
        raw.append(0)  # filter type 0
        raw.extend(row)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def render_luxury_artwork(path: Path, width: int, height: int, accent_rgb: tuple[int, int, int], motif: str) -> None:
    ar, ag, ab = accent_rgb
    gr, gg, gb = (214, 178, 108)  # Champagne Gold
    rows: list[bytes] = []

    for y in range(height):
        fy = y / max(1, height - 1)
        row = bytearray(width * 3)
        border_y = y < 6 or y >= height - 6
        inner_y = (14 <= y < 17) or (height - 17 <= y < height - 14)

        for x in range(width):
            fx = x / max(1, width - 1)
            border_x = x < 6 or x >= width - 6
            inner_x = (14 <= x < 17) or (width - 17 <= x < width - 14)

            if border_x or border_y:
                r, g, b = gr, gg, gb
            elif (inner_x and 14 <= y < height - 14) or (inner_y and 14 <= x < width - 14):
                r, g, b = (72, 82, 98)
            else:
                # Brushed gunmetal diagonal sheen + radial accent glow
                sheen = 0.5 + 0.5 * math.sin((fx * 3.5 + fy * 2.2) * math.pi)
                base_r = int(18 + 18 * (1.0 - fy) + 8 * sheen)
                base_g = int(21 + 20 * (1.0 - fy) + 9 * sheen)
                base_b = int(27 + 25 * (1.0 - fy) + 11 * sheen)

                dx = (fx - 0.5) * 2.0
                dy = (fy - 0.5) * 2.0
                dist = math.sqrt(dx * dx + dy * dy)
                glow = max(0.0, 1.0 - dist * 1.15) ** 2.2

                r = min(255, int(base_r + ar * glow * 0.55))
                g = min(255, int(base_g + ag * glow * 0.55))
                b = min(255, int(base_b + ab * glow * 0.55))

                # Crisp geometric centerpiece emblem
                if motif == "breaker":
                    # Stylized brick grid + glowing orb + gold paddle
                    if 0.24 <= fy <= 0.44 and 0.20 <= fx <= 0.80:
                        bx_mod = (x - int(width * 0.20)) % max(12, width // 9)
                        by_mod = (y - int(height * 0.24)) % max(8, height // 14)
                        if bx_mod > 3 and by_mod > 3:
                            r, g, b = (gr, gg, gb) if ((y // max(8, height // 14)) % 2 == 0) else (ar, ag, ab)
                    elif (fx - 0.52) ** 2 + (fy - 0.60) ** 2 < 0.0045:
                        r, g, b = (240, 248, 255)
                    elif 0.76 <= fy <= 0.80 and 0.34 <= fx <= 0.66:
                        r, g, b = gr, gg, gb
                elif motif == "overdrive":
                    # Synthwave sun + perspective highway lines
                    if fy < 0.54 and (fx - 0.5) ** 2 + (fy - 0.42) ** 2 < 0.038:
                        if (y // max(3, height // 64)) % 2 == 0 or fy < 0.38:
                            r, g, b = (245, 198, 112)
                    elif fy >= 0.54:
                        t = (fy - 0.54) / 0.46
                        half_w = 0.08 + t * 0.38
                        edge = abs(abs(fx - 0.5) - half_w)
                        center_dash = abs(fx - 0.5) < 0.012 and ((y // max(4, height // 36)) % 2 == 0)
                        if edge < 0.014:
                            r, g, b = gr, gg, gb
                        elif center_dash:
                            r, g, b = ar, ag, ab
                else:
                    # Hardware Lab precision oscilloscope ring + resolution bars
                    ring = abs(dist - 0.46)
                    if ring < 0.022:
                        r, g, b = gr, gg, gb
                    elif 0.68 <= fy <= 0.78 and 0.22 <= fx <= 0.78:
                        seg = int(((fx - 0.22) / 0.56) * 6)
                        palette = [
                            (214, 178, 108),
                            (76, 208, 238),
                            (78, 209, 146),
                            (174, 118, 242),
                            (232, 88, 104),
                            (228, 233, 240),
                        ]
                        r, g, b = palette[min(5, max(0, seg))]

            off = x * 3
            row[off] = r
            row[off + 1] = g
            row[off + 2] = b
        rows.append(bytes(row))

    write_png_rgb(path, width, height, rows)


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: build_store_packages.py <path/to/razor_zar_packer>", file=sys.stderr)
        return 1

    packer_bin = Path(sys.argv[1]).resolve()
    if not packer_bin.exists():
        raise FileNotFoundError(f"Missing razor_zar_packer binary: {packer_bin}")

    repo_root = Path(__file__).resolve().parents[2]
    src_dir = Path(__file__).resolve().parent
    store_dir = repo_root / "store"
    packages_dir = store_dir / "packages"
    icons_dir = store_dir / "icons"
    packages_dir.mkdir(parents=True, exist_ok=True)
    icons_dir.mkdir(parents=True, exist_ok=True)

    specs = [
        {
            "title_id": "PPSA90001",
            "source": "cyber_breaker.cpp",
            "name": "Razor Cyber Breaker",
            "subtitle": "Native PS5 Prospero 60 FPS Gunmetal & Neon Arcade Brick-Breaker",
            "category": "Arcade Game",
            "version": "01.00",
            "accent": (76, 208, 238),
            "motif": "breaker",
            "controls": "DualSense Left Stick / D-Pad (or A/D / Left/Right), Cross / R2 to Launch, L1/R1 Speed Boost",
            "description": (
                "Shatter multi-layered armored cyber-cores at locked 60 FPS VSync! Built natively for the "
                "PlayStation 5 Prospero VideoOut & DualSense stack with automatic 1080p FHD, 1440p WQHD, "
                "and 4K UHD render-surface scaling, particle bursts, combo multipliers, and progressive waves."
            ),
        },
        {
            "title_id": "PPSA90002",
            "source": "neon_overdrive.cpp",
            "name": "Razor Neon Overdrive",
            "subtitle": "Native PS5 Prospero 60 FPS Synthwave Highway Hypercar Racer",
            "category": "Racing Game",
            "version": "01.00",
            "accent": (232, 88, 104),
            "motif": "overdrive",
            "controls": "DualSense Left Stick / D-Pad to Steer, Cross / R2 for 360 KM/H Nitrous Boost, Square / L2 Brake",
            "description": (
                "Tear down a 4-lane synthwave sunset highway at 360 KM/H! Features perspective scanline "
                "projection, rival traffic waves, collectible Champagne-Gold shield cores, Nitrous overdrive "
                "flames, and native PS5 1080p / 1440p / 4K UHD output detection."
            ),
        },
        {
            "title_id": "PPSA90003",
            "source": "hardware_lab.cpp",
            "name": "Razor Hardware & Resolution Lab",
            "subtitle": "Interactive PS5 Prospero 1080p/1440p/4K Display & DualSense Diagnostic Suite",
            "category": "Diagnostics & Demo",
            "version": "01.00",
            "accent": (78, 209, 146),
            "motif": "lab",
            "controls": "Move Sticks, Pull L2/R2 Triggers, Press Face/D-Pad Buttons, Press Triangle to Cycle Lightbar",
            "description": (
                "Verify your RazorEmulations PC setup in real time! Queries PS5 sceVideoOutGetOutputStatus "
                "(1920x1080 FHD, 2560x1440 WQHD, or 3840x2160 4K UHD), visualizes DualSense analog sticks, "
                "triggers, buttons, and Lightbar colors, and runs a 60 Hz VSync motion-pacing bar."
            ),
        },
    ]

    catalog_items = []

    with tempfile.TemporaryDirectory(prefix="razor_prospero_build_") as tmp_root:
        tmp_path = Path(tmp_root)

        for spec in specs:
            tid = spec["title_id"]
            game_stage = tmp_path / tid
            sce_sys = game_stage / "sce_sys"
            sce_sys.mkdir(parents=True, exist_ok=True)

            eboot_path = game_stage / "eboot.bin"
            cpp_path = src_dir / spec["source"]

            cmd = [
                "clang++",
                "--target=x86_64-unknown-freebsd13",
                "-fuse-ld=lld",
                "-std=c++20",
                "-O2",
                "-ffreestanding",
                "-fPIC",
                "-fvisibility=hidden",
                "-mno-red-zone",
                "-fno-stack-protector",
                "-fno-exceptions",
                "-fno-rtti",
                "-nostdlib",
                f"-I{src_dir}",
                "-Wl,--eh-frame-hdr",
                "-Wl,--hash-style=sysv",
                "-Wl,-z,lazy",
                "-Wl,-z,norelro",
                "-Wl,-z,max-page-size=0x4000",
                "-Wl,--unresolved-symbols=ignore-all",
                "-Wl,--export-dynamic",
                "-Wl,-shared",
                "-Wl,-e,_start",
                *[f"-Wl,--auxiliary=slot{k}" for k in range(9)],
                str(cpp_path),
                "-o",
                str(eboot_path),
            ]
            subprocess.run(cmd, check=True)
            patch_prospero_elf(eboot_path)

            # Generate PS5 sce_sys/param.json, icon0.png (256x256), and pic0.png (960x540)
            param_json = {
                "titleId": tid,
                "contentId": f"UP9000-{tid}_00-RAZORSTORE000001",
                "contentVersion": spec["version"],
                "masterVersion": spec["version"],
                "systemFileVersion": "06.00.00.00",
                "localizedParameters": {
                    "defaultLanguage": "en-US",
                    "en-US": {"titleName": spec["name"]},
                },
            }
            (sce_sys / "param.json").write_text(json.dumps(param_json, indent=2), encoding="utf-8")

            icon_path = sce_sys / "icon0.png"
            pic_path = sce_sys / "pic0.png"
            render_luxury_artwork(icon_path, 256, 256, spec["accent"], spec["motif"])
            render_luxury_artwork(pic_path, 960, 540, spec["accent"], spec["motif"])

            store_icon = icons_dir / f"{tid}.png"
            shutil.copy2(icon_path, store_icon)

            zar_out = packages_dir / f"{tid}.zar"
            subprocess.run([str(packer_bin), str(game_stage), str(zar_out)], check=True)

            zar_bytes = zar_out.read_bytes()
            sha256_hex = hashlib.sha256(zar_bytes).hexdigest()

            catalog_items.append(
                {
                    "title_id": tid,
                    "name": spec["name"],
                    "subtitle": spec["subtitle"],
                    "category": spec["category"],
                    "version": spec["version"],
                    "author": "RazorEmulations Studio",
                    "license": "GPL-2.0 / Open PS5 Homebrew",
                    "resolutions": "1080p FHD · 1440p WQHD · 4K UHD",
                    "controls": spec["controls"],
                    "description": spec["description"],
                    "format": "zar",
                    "size_bytes": len(zar_bytes),
                    "sha256": sha256_hex,
                    "download_url": f"https://raw.githubusercontent.com/RazorCz36/Razor-emulation/main/store/packages/{tid}.zar",
                    "local_fallback": f"packages/{tid}.zar",
                    "icon_url": f"https://raw.githubusercontent.com/RazorCz36/Razor-emulation/main/store/icons/{tid}.png",
                    "local_icon": f"icons/{tid}.png",
                }
            )
            print(f"[OK] Built {tid}.zar ({len(zar_bytes)} bytes, sha256={sha256_hex[:16]}...)")

    catalog = {
        "store_name": "RazorEmulations Official PS5 Homebrew & Diagnostics Hub",
        "store_version": 1,
        "updated_at": "2026-11-21",
        "items": catalog_items,
    }
    catalog_path = store_dir / "catalog.json"
    catalog_path.write_text(json.dumps(catalog, indent=2) + "\n", encoding="utf-8")
    print(f"[OK] Wrote {catalog_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
