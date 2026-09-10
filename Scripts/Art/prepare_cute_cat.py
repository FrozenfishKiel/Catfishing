"""Preserve the Blender armature as a common root for UE's legacy FBX importer.

UE deliberately strips a top-level Blender node named Armature. This source has
multiple parentless bones beneath it. Rename only that FBX Model name to Cat_Root
(same byte length); all geometry, bind poses, IDs, connections and animation data
remain byte-identical. Without --output, only inspect the source.
"""

import argparse
import json
from pathlib import Path
import struct


def inspect(data):
    assert data[:23] == b"Kaydara FBX Binary  \x00\x1a\x00", "Expected binary FBX"
    version = struct.unpack_from("<I", data, 23)[0]
    header = "<QQQB" if version >= 7500 else "<IIIB"
    header_size = struct.calcsize(header)
    nodes = []

    def node(offset, parent):
        end, count, prop_size, name_size = struct.unpack_from(header, data, offset)
        if end == 0:
            return None
        start = offset + header_size
        name = bytes(data[start:start + name_size]).decode()
        pos = start + name_size
        prop_end = pos + prop_size
        values = []
        strings = []
        for _ in range(count):
            kind = chr(data[pos])
            pos += 1
            if kind in "SR":
                length = struct.unpack_from("<I", data, pos)[0]
                pos += 4
                value = bytes(data[pos:pos + length])
                strings.append((pos, value))
                values.append(value.decode("utf-8", "replace"))
                pos += length
            elif kind in "YCBILFD":
                fmt = {"Y": "h", "C": "?", "B": "b", "I": "i", "L": "q", "F": "f", "D": "d"}[kind]
                values.append(struct.unpack_from("<" + fmt, data, pos)[0])
                pos += struct.calcsize(fmt)
            elif kind in "fdlibc":
                _, _, length = struct.unpack_from("<III", data, pos)
                pos += 12 + length
                values.append("<array>")
            else:
                raise ValueError("Unknown property type " + kind)
        assert pos == prop_end
        nodes.append((parent, name, values, strings))
        while pos + header_size <= end:
            next_pos = node(pos, name)
            if next_pos is None:
                break
            pos = next_pos
        return end

    pos = 27
    while pos + header_size <= len(data):
        end = node(pos, "")
        if end is None:
            break
        pos = end
    return version, nodes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True)
    parser.add_argument("--output")
    args = parser.parse_args()
    data = bytearray(Path(args.source).read_bytes())
    version, nodes = inspect(data)
    models = [n for n in nodes if n[0] == "Objects" and n[1] == "Model"]
    connections = [n[2] for n in nodes if n[0] == "Connections" and n[1] == "C"]
    model_ids = {n[2][0] for n in models}
    parents = {v[1]: v[2] for v in connections if v[0] == "OO" and v[1] in model_ids
               and (v[2] in model_ids or v[2] == 0)}
    print(json.dumps({"version": version,
        "models": [{"id": n[2][0], "name": n[2][1], "type": n[2][2], "parent": parents.get(n[2][0])} for n in models],
        "materials": [n[2] for n in nodes if n[0] == "Objects" and n[1] == "Material"],
        "animations": [n[2] for n in nodes if n[0] == "Objects" and n[1] == "AnimationStack"]}, ensure_ascii=False))
    if not args.output:
        return
    roots = [n for n in models if n[2][1].split("\x00")[0] == "Armature" and parents.get(n[2][0], 0) == 0]
    assert len(roots) == 1, "Expected exactly one top-level Armature model"
    root = roots[0]
    assert root[2][2] == "Null", "Expected Blender armature Null node"
    offset, name = root[3][0]
    assert name.startswith(b"Armature\x00"), "Unexpected FBX name encoding"
    data[offset:offset + 8] = b"Cat_Root"
    output = Path(args.output)
    assert output.resolve() != Path(args.source).resolve(), "Never overwrite source"
    assert not output.exists(), "Refusing to overwrite output"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)
    original = Path(args.source).read_bytes()
    changed = [i for i, (a, b) in enumerate(zip(original, data)) if a != b]
    assert len(original) == len(data) and all(offset <= i < offset + 8 for i in changed)
    print(json.dumps({"event": "cute_cat_root_prepared", "output": str(output),
                      "old_name": "Armature", "new_name": "Cat_Root", "changed_bytes": len(changed)}))


if __name__ == "__main__":
    main()
