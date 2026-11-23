#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
"""Attach/check advisory ISOBMFF stereo packing on a libavif fixture.

The input is a small self-contained fixture with one colour 'pict' track.
Move rewritten 'moov' to the end so original media and item offsets stay valid.
ISO/IEC 14496-12:2026 §13.5.4.2, scheme 4. The HEIF §6.8.5 item group is separate.
"""
import argparse
import struct
from pathlib import Path

STVI = bytes.fromhex("00000000 00000003 00000004 00000002 0300")


def box(kind, body):
    return struct.pack(">I4s", 8 + len(body), kind) + body


def boxes(data):
    offset = 0
    while offset < len(data):
        if len(data) - offset < 8:
            raise ValueError("truncated box header")
        size, kind = struct.unpack_from(">I4s", data, offset)
        if size < 8 or offset + size > len(data):
            raise ValueError(f"unsupported or malformed {kind!r} box")
        yield kind, data[offset+8:offset+size]
        offset += size


def mark(data, check=False, match_alpha_display=False):
    def descend(kind, body):
        if kind == b"trak":
            media = next(payload for name, payload in boxes(body) if name == b"mdia")
            handler = next(payload for name, payload in boxes(media) if name == b"hdlr")
            if handler[8:12] != b"pict":
                if match_alpha_display and handler[8:12] == b"auxv" and not check:
                    return b"".join(box(name, descend(name, payload) if name == b"tkhd" else payload)
                                    for name, payload in boxes(body))
                return body
            if state["tracks"]:
                raise ValueError("expected one colour picture track")
            state["tracks"] += 1
        if kind == b"tkhd":
            if not check:
                width, height = struct.unpack(">II", body[-8:])
                if width != 2 * height:
                    raise ValueError("expected packed square fixture views")
                return body[:-8] + struct.pack(">II", width // 2, height)
            width, height = struct.unpack(">II", body[-8:])
            if width != height:
                raise ValueError("track presentation size is not a view's size")
        if kind == b"stsd":
            if body[:8] != struct.pack(">II", 0, 1):
                raise ValueError("expected one visual sample entry")
            parts = list(boxes(body[8:]))
            if len(parts) != 1 or parts[0][0] != b"av01":
                raise ValueError("expected av01")
            sample = parts[0][1]
            if len(sample) < 78:
                raise ValueError("invalid visual sample entry")
            width, height = struct.unpack_from(">HH", sample, 24)
            if width != 2 * height:
                raise ValueError("sample is not two square views")
            state["entry"] += 1
            rinf = box(b"rinf", box(b"schm", b"\0" * 4 + b"stvi" + b"\0\1\0\0")
                       + box(b"schi", box(b"stvi", STVI)))
            existing = [payload for name, payload in boxes(sample[78:]) if name == b"rinf"]
            if check:
                if len(existing) != 1:
                    raise ValueError("missing or duplicate scheme")
                scheme = dict(boxes(existing[0]))
                if scheme.get(b"schm") != b"\0" * 4 + b"stvi" + b"\0\1\0\0":
                    raise ValueError("wrong restricted scheme")
                if list(boxes(scheme[b"schi"])) != [(b"stvi", STVI)]:
                    raise ValueError("wrong stereo packing")
                return body
            if existing:
                raise ValueError("fixture is already marked")
            return body[:8] + box(b"av01", sample + rinf)
        if kind in (b"moov", b"trak", b"mdia", b"minf", b"stbl"):
            return b"".join(box(name, descend(name, payload)) for name, payload in boxes(body))
        return body

    state = dict(tracks=0, entry=0)
    parts = list(boxes(data))
    movies = [body for name, body in parts if name == b"moov"]
    if len(movies) != 1:
        raise ValueError("expected precisely one moov")
    movie = descend(b"moov", movies[0])
    if state != dict(tracks=1, entry=1):
        raise ValueError("missing picture track or AV1 sample entry")
    if check:
        return data
    return b"".join(box(b"free", bytes(len(body))) if name == b"moov" else box(name, body)
                    for name, body in parts) + box(b"moov", movie)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument("--match-alpha-display", action="store_true")
    args = parser.parse_args()
    data = args.input.read_bytes()
    if args.output:
        data = mark(data, match_alpha_display=args.match_alpha_display)
        mark(data, check=True)
        args.output.write_bytes(data)
    else:
        mark(data, check=True)
    print("AVIF pict: rinf/schi/stvi, scheme 4, side by side, left first, whole samples")
