#!/usr/bin/env python3
"""tests/GltfTests.cpp가 쓰는 작은 glTF 표본을 만든다. 출력은 저장소에 커밋되어 있으므로
표본을 바꿀 때만 실행한다:  python3 tests/fixtures/gltf/generate.py

- textured-quad.gltf + textured-quad.bin + checker.png: 외부 .bin, 외부 PNG, ushort 인덱스, 선 primitive(건너뜀 경고)
- hierarchy.gltf: 모든 버퍼가 data URI. node 계층(TRS/matrix/인스턴싱), 삼각형 strip, sparse accessor,
  normalized UV, skin(JOINTS_0/WEIGHTS_0), 애니메이션(LINEAR/STEP/CUBICSPLINE)
- hierarchy.glb: 같은 내용 + bufferView에 넣은 PNG 텍스처를 GLB BIN 청크 하나에 담는다
- skinned-tube.glb: 관절 2개(Base, Bend)로 휘는 원통(5단계 애니메이션 테스트·골든 anim.png).
  클립 "Bend"(Bend 회전 LINEAR, Base 이동 STEP)와 "Twist"(Base 회전 CUBICSPLINE)
"""
import base64
import json
import math
import struct
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent


def png_rgba(width, height, pixels):
    """pixels: 행 순서(위→아래) RGBA 튜플 목록."""
    raw = b"".join(
        b"\x00" + b"".join(bytes(pixels[y * width + x]) for x in range(width)) for y in range(height)
    )

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


# 2x2: 왼쪽 위 빨강, 오른쪽 위 초록, 왼쪽 아래 파랑, 오른쪽 아래 흰색
CHECKER = png_rgba(2, 2, [(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (255, 255, 255, 255)])


class Builder:
    """버퍼 하나에 bufferView/accessor를 차례로 쌓는다. 각 bufferView는 4바이트 정렬."""

    def __init__(self):
        self.data = bytearray()
        self.doc = {"asset": {"version": "2.0", "generator": "SoftwareRenderer test fixtures"},
                    "bufferViews": [], "accessors": []}

    def view(self, payload, stride=None, target=None):
        while len(self.data) % 4:
            self.data.append(0)
        view = {"buffer": 0, "byteOffset": len(self.data), "byteLength": len(payload)}
        if stride:
            view["byteStride"] = stride
        if target:
            view["target"] = target
        self.data += payload
        self.doc["bufferViews"].append(view)
        return len(self.doc["bufferViews"]) - 1

    def accessor(self, fmt, component, kind, values, normalized=False, minmax=False, **extra):
        per = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[kind]
        flat = [v for item in values for v in (item if isinstance(item, (list, tuple)) else [item])]
        payload = struct.pack("<" + fmt * len(flat), *flat)
        accessor = {"bufferView": self.view(payload), "componentType": component, "count": len(flat) // per,
                    "type": kind}
        if normalized:
            accessor["normalized"] = True
        if minmax:
            accessor["min"] = [min(flat[i::per]) for i in range(per)]
            accessor["max"] = [max(flat[i::per]) for i in range(per)]
        accessor.update(extra)
        self.doc["accessors"].append(accessor)
        return len(self.doc["accessors"]) - 1


FLOAT, UBYTE, USHORT = 5126, 5121, 5123


def textured_quad():
    b = Builder()
    positions = b.accessor("f", FLOAT, "VEC3", [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)], minmax=True)
    normals = b.accessor("f", FLOAT, "VEC3", [(0, 0, 1)] * 4)
    uvs = b.accessor("f", FLOAT, "VEC2", [(0, 1), (1, 1), (1, 0), (0, 0)])
    indices = b.accessor("H", USHORT, "SCALAR", [0, 1, 2, 0, 2, 3])
    lines = b.accessor("H", USHORT, "SCALAR", [0, 1])
    b.doc.update({
        "scene": 0,
        "scenes": [{"name": "Quad scene", "nodes": [0]}],
        "nodes": [{"name": "Quad", "mesh": 0}],
        "meshes": [{"name": "Quad", "primitives": [
            {"attributes": {"POSITION": positions, "NORMAL": normals, "TEXCOORD_0": uvs}, "indices": indices,
             "material": 0},
            {"attributes": {"POSITION": positions}, "indices": lines, "mode": 1},
        ]}],
        "materials": [{"name": "Checker", "pbrMetallicRoughness": {
            "baseColorFactor": [1.0, 0.5, 0.25, 1.0], "baseColorTexture": {"index": 0},
            "metallicFactor": 0.0, "roughnessFactor": 0.5}}],
        "textures": [{"source": 0, "sampler": 0}],
        "samplers": [{"magFilter": 9728, "minFilter": 9728}],
        "images": [{"uri": "checker.png"}],
        "buffers": [{"uri": "textured-quad.bin", "byteLength": len(b.data)}],
    })
    (HERE / "textured-quad.bin").write_bytes(bytes(b.data))
    (HERE / "checker.png").write_bytes(CHECKER)
    (HERE / "textured-quad.gltf").write_text(json.dumps(b.doc, indent=2) + "\n", encoding="utf-8")


def quat_axis_angle(axis, angle):
    s = math.sin(angle / 2)
    return [axis[0] * s, axis[1] * s, axis[2] * s, math.cos(angle / 2)]


def hierarchy(embed_image):
    b = Builder()
    # primitive 0: 법선 없는 삼각형(인덱스 없음) + skin 속성
    tri = b.accessor("f", FLOAT, "VEC3", [(0, 0, 0), (1, 0, 0), (0, 1, 0)], minmax=True)
    joints = b.accessor("B", UBYTE, "VEC4", [(0, 1, 0, 0), (1, 0, 0, 0), (0, 1, 0, 0)])
    weights = b.accessor("f", FLOAT, "VEC4", [(0.75, 0.25, 0, 0), (1, 0, 0, 0), (0.5, 0.5, 0, 0)])
    # primitive 1: 삼각형 strip(정점 4개, ubyte 인덱스). 위치 accessor는 sparse로 정점 3의 z를 바꾼다.
    strip_base = [(0, 0, 1), (1, 0, 1), (0, 1, 1), (1, 1, 1)]
    strip = b.accessor("f", FLOAT, "VEC3", strip_base, minmax=True)
    sparse_indices = b.view(struct.pack("<B", 3))
    sparse_values = b.view(struct.pack("<3f", 1, 1, 2))
    b.doc["accessors"][strip]["sparse"] = {
        "count": 1, "indices": {"bufferView": sparse_indices, "componentType": UBYTE},
        "values": {"bufferView": sparse_values}}
    strip_uv = b.accessor("B", UBYTE, "VEC2", [(0, 255), (255, 255), (0, 0), (255, 0)], normalized=True)
    strip_indices = b.accessor("B", UBYTE, "SCALAR", [0, 1, 2, 3])
    inverse_bind = b.accessor("f", FLOAT, "MAT4", [
        [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1],
        [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1]])
    times_linear = b.accessor("f", FLOAT, "SCALAR", [0.0, 1.0], minmax=True)
    translations = b.accessor("f", FLOAT, "VEC3", [(0, 1, 0), (0, 2, 0)])
    times_step = b.accessor("f", FLOAT, "SCALAR", [0.0, 0.5], minmax=True)
    rotations = b.accessor("f", FLOAT, "VEC4", [quat_axis_angle((0, 1, 0), 0), quat_axis_angle((0, 1, 0), math.pi)])
    times_cubic = b.accessor("f", FLOAT, "SCALAR", [0.0, 2.0], minmax=True)
    scales = b.accessor("f", FLOAT, "VEC3", [(0, 0, 0), (1, 1, 1), (0, 0, 0), (0, 0, 0), (2, 2, 2), (0, 0, 0)])

    material = {"name": "Orange", "pbrMetallicRoughness": {"baseColorFactor": [1.0, 0.5, 0.0, 1.0]}}
    doc = b.doc
    doc.update({
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [
            {"name": "Root", "translation": [1, 0, 0], "children": [1, 2]},
            {"name": "Spinner", "rotation": quat_axis_angle((0, 1, 0), math.pi / 2), "mesh": 0, "skin": 0,
             "children": [3]},
            # T(0,2,0) * S(2) 를 열 우선으로
            {"name": "Matrix", "matrix": [2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0, 2, 0, 1], "mesh": 0},
            {"name": "Joint", "translation": [0, 1, 0]},
        ],
        "meshes": [{"name": "Shapes", "primitives": [
            {"attributes": {"POSITION": tri, "JOINTS_0": joints, "WEIGHTS_0": weights}, "material": 0},
            {"attributes": {"POSITION": strip, "TEXCOORD_0": strip_uv}, "indices": strip_indices, "mode": 5,
             "material": 0},
        ]}],
        "materials": [material],
        "skins": [{"name": "Rig", "joints": [1, 3], "inverseBindMatrices": inverse_bind, "skeleton": 1}],
        "animations": [{"name": "Wiggle", "samplers": [
            {"input": times_linear, "output": translations},
            {"input": times_step, "output": rotations, "interpolation": "STEP"},
            {"input": times_cubic, "output": scales, "interpolation": "CUBICSPLINE"},
        ], "channels": [
            {"sampler": 0, "target": {"node": 3, "path": "translation"}},
            {"sampler": 1, "target": {"node": 1, "path": "rotation"}},
            {"sampler": 2, "target": {"node": 0, "path": "scale"}},
        ]}],
    })
    if embed_image:
        image_view = b.view(CHECKER)
        doc["images"] = [{"bufferView": image_view, "mimeType": "image/png"}]
        doc["textures"] = [{"source": 0}]
        material["pbrMetallicRoughness"]["baseColorTexture"] = {"index": 0}
    return doc, bytes(b.data)


def write_gltf_embedded():
    doc, data = hierarchy(embed_image=False)
    doc["buffers"] = [{"byteLength": len(data),
                       "uri": "data:application/octet-stream;base64," + base64.b64encode(data).decode()}]
    (HERE / "hierarchy.gltf").write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")


def write_glb():
    doc, data = hierarchy(embed_image=True)
    doc["buffers"] = [{"byteLength": len(data)}]
    text = json.dumps(doc, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    data += b"\x00" * (-len(data) % 4)
    chunks = struct.pack("<II", len(text), 0x4E4F534A) + text + struct.pack("<II", len(data), 0x004E4942) + data
    (HERE / "hierarchy.glb").write_bytes(struct.pack("<III", 0x46546C67, 2, 12 + len(chunks)) + chunks)


def skinned_tube():
    """y=0..2, 반지름 0.3인 뚜껑 없는 원통. y<=0.75는 Base, y>=1.25는 Bend에 붙고 그 사이는 선형으로 섞인다."""
    b = Builder()
    rings, segments, height, radius = 13, 16, 2.0, 0.3
    positions, normals, joints, weights, indices = [], [], [], [], []
    for r in range(rings):
        y = height * r / (rings - 1)
        bend = min(max((y - 0.75) / 0.5, 0.0), 1.0)
        for k in range(segments):
            angle = 2 * math.pi * k / segments
            c, s = math.cos(angle), math.sin(angle)
            positions.append((radius * c, y, radius * s))
            normals.append((c, 0.0, s))
            joints.append((0, 1, 0, 0))
            weights.append((1.0 - bend, bend, 0.0, 0.0))
    for r in range(rings - 1):
        for k in range(segments):
            a, b2 = r * segments + k, r * segments + (k + 1) % segments
            c2, d = a + segments, b2 + segments
            # glTF 앞면은 반시계(바깥에서 볼 때)
            indices += [a, c2, b2, b2, c2, d]
    pos = b.accessor("f", FLOAT, "VEC3", positions, minmax=True)
    nrm = b.accessor("f", FLOAT, "VEC3", normals)
    jnt = b.accessor("B", UBYTE, "VEC4", joints)
    wgt = b.accessor("f", FLOAT, "VEC4", weights)
    idx = b.accessor("H", USHORT, "SCALAR", indices)
    inverse_bind = b.accessor("f", FLOAT, "MAT4", [
        [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1],
        [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1]])
    bend_times = b.accessor("f", FLOAT, "SCALAR", [0.0, 1.0, 2.0], minmax=True)
    bend_values = b.accessor("f", FLOAT, "VEC4", [quat_axis_angle((0, 0, 1), 0), quat_axis_angle((0, 0, 1), math.pi / 2),
                                                 quat_axis_angle((0, 0, 1), 0)])
    step_times = b.accessor("f", FLOAT, "SCALAR", [0.0, 1.0], minmax=True)
    step_values = b.accessor("f", FLOAT, "VEC3", [(0, 0, 0), (0.25, 0, 0)])
    twist_times = b.accessor("f", FLOAT, "SCALAR", [0.0, 1.5], minmax=True)
    zero = (0, 0, 0, 0)
    twist_values = b.accessor("f", FLOAT, "VEC4", [zero, quat_axis_angle((0, 1, 0), 0), zero,
                                                  zero, quat_axis_angle((0, 1, 0), math.pi / 2), zero])
    doc = b.doc
    doc.update({
        "scene": 0,
        "scenes": [{"nodes": [0, 1]}],
        "nodes": [
            {"name": "Tube", "mesh": 0, "skin": 0},
            {"name": "Base", "children": [2]},
            {"name": "Bend", "translation": [0, 1, 0]},
        ],
        "meshes": [{"name": "Tube", "primitives": [
            {"attributes": {"POSITION": pos, "NORMAL": nrm, "JOINTS_0": jnt, "WEIGHTS_0": wgt}, "indices": idx,
             "material": 0}]}],
        "materials": [{"name": "Teal", "pbrMetallicRoughness": {"baseColorFactor": [0.2, 0.7, 0.8, 1.0],
                                                                 "metallicFactor": 0.0}}],
        "skins": [{"name": "Rig", "joints": [1, 2], "inverseBindMatrices": inverse_bind, "skeleton": 1}],
        "animations": [
            {"name": "Bend", "samplers": [
                {"input": bend_times, "output": bend_values},
                {"input": step_times, "output": step_values, "interpolation": "STEP"},
            ], "channels": [
                {"sampler": 0, "target": {"node": 2, "path": "rotation"}},
                {"sampler": 1, "target": {"node": 1, "path": "translation"}},
            ]},
            {"name": "Twist", "samplers": [{"input": twist_times, "output": twist_values,
                                            "interpolation": "CUBICSPLINE"}],
             "channels": [{"sampler": 0, "target": {"node": 1, "path": "rotation"}}]},
        ],
    })
    return doc, bytes(b.data)


def write_glb_file(name, doc, data):
    doc["buffers"] = [{"byteLength": len(data)}]
    text = json.dumps(doc, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    data += b"\x00" * (-len(data) % 4)
    chunks = struct.pack("<II", len(text), 0x4E4F534A) + text + struct.pack("<II", len(data), 0x004E4942) + data
    (HERE / name).write_bytes(struct.pack("<III", 0x46546C67, 2, 12 + len(chunks)) + chunks)


if __name__ == "__main__":
    textured_quad()
    write_gltf_embedded()
    write_glb()
    write_glb_file("skinned-tube.glb", *skinned_tube())
