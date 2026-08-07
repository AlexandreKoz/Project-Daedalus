#!/usr/bin/env python3
"""Generate small self-authored Campaign B glTF/GLB fixtures."""
from __future__ import annotations

import base64
import json
import math
import shutil
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent
VALID = ROOT / "valid"
INVALID = ROOT / "invalid"

# Self-authored 1x1 RGB baseline JPEG encoded once and embedded here so fixture
# regeneration has no Pillow or external-tool dependency.
JPEG_1X1_RGB = base64.b64decode(
    "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAIBAQEBAQIBAQECAgICAgQDAgICAgUEBAMEBgUGBgYFBgYGBwkIBgcJBwYGCAsICQoKCgoKBggLDAsKDAkKCgr/"
    "2wBDAQICAgICAgUDAwUKBwYHCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgr/wAARCAABAAEDASIAAhEBAxEB/"
    "8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2Jy"
    "ggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLD"
    "xMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3"
    "AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6"
    "goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwDyuiiiv78P4/P/2Q=="
)
for generated_directory in (VALID, INVALID):
    generated_directory.mkdir(parents=True, exist_ok=True)
    for generated_path in generated_directory.iterdir():
        if generated_path.is_dir():
            shutil.rmtree(generated_path)
        else:
            generated_path.unlink()


def png_rgba(width: int, height: int, rgba: bytes) -> bytes:
    assert len(rgba) == width * height * 4
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    scan = b"".join(b"\x00" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(scan)) + chunk(b"IEND", b"")


def align4(data: bytes, pad: bytes = b"\x00") -> bytes:
    return data + pad * ((4 - len(data) % 4) % 4)


def write_glb(path: Path, document: dict, binary: bytes) -> None:
    json_bytes = align4(json.dumps(document, separators=(",", ":"), ensure_ascii=False).encode("utf-8"), b" ")
    binary = align4(binary)
    total = 12 + 8 + len(json_bytes) + (8 + len(binary) if binary else 0)
    output = struct.pack("<III", 0x46546C67, 2, total)
    output += struct.pack("<II", len(json_bytes), 0x4E4F534A) + json_bytes
    if binary:
        output += struct.pack("<II", len(binary), 0x004E4942) + binary
    path.write_bytes(output)


def data_uri(data: bytes, mime: str = "application/octet-stream") -> str:
    return f"data:{mime};base64," + base64.b64encode(data).decode("ascii")


# Minimal valid GLB.
minimal_bin = struct.pack("<9f3H", 0.0, 0.6, 0.0, 0.6, -0.6, 0.0, -0.6, -0.6, 0.0, 0, 1, 2)
minimal_doc = {
    "asset": {"version": "2.0", "generator": "Daedalus fixture generator"},
    "buffers": [{"byteLength": len(minimal_bin)}],
    "bufferViews": [
        {"buffer": 0, "byteOffset": 0, "byteLength": 36},
        {"buffer": 0, "byteOffset": 36, "byteLength": 6},
    ],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
        {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"},
    ],
    "meshes": [{"name": "MinimalTriangle", "primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
    "nodes": [{"name": "TriangleNode", "mesh": 0}],
    "scenes": [{"name": "Minimal", "nodes": [0]}],
    "scene": 0,
}
write_glb(VALID / "minimal.glb", minimal_doc, minimal_bin)

# External-resource, multi-primitive, strided, hierarchy, negative scale, camera/light scene.
vertices = [
    (-1.0, -1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0),
    (1.0, -1.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0),
    (1.0, 1.0, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0),
    (-1.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0),
]
external_bin = b"".join(struct.pack("<8f", *vertex) for vertex in vertices)
index0_offset = len(external_bin)
external_bin += struct.pack("<3H", 0, 1, 2)
external_bin = align4(external_bin)
index1_offset = len(external_bin)
external_bin += struct.pack("<3H", 0, 2, 3)
(VALID / "external_scene.bin").write_bytes(external_bin)
(VALID / "texture.png").write_bytes(png_rgba(2, 2, bytes([
    255, 0, 0, 255, 0, 255, 0, 255,
    0, 0, 255, 255, 255, 255, 255, 255,
])))
(VALID / "texture_changed.png").write_bytes(png_rgba(2, 2, bytes([
    255, 255, 0, 255, 0, 255, 255, 255,
    255, 0, 255, 255, 32, 32, 32, 255,
])))
external_doc = {
    "asset": {"version": "2.0", "generator": "Daedalus fixture generator", "copyright": "CC0 self-authored fixture"},
    "extensionsUsed": ["KHR_lights_punctual"],
    "buffers": [{"uri": "external_scene.bin", "byteLength": len(external_bin)}],
    "bufferViews": [
        {"buffer": 0, "byteOffset": 0, "byteLength": 128, "byteStride": 32},
        {"buffer": 0, "byteOffset": index0_offset, "byteLength": 6},
        {"buffer": 0, "byteOffset": index1_offset, "byteLength": 6},
    ],
    "accessors": [
        {"bufferView": 0, "byteOffset": 0, "componentType": 5126, "count": 4, "type": "VEC3"},
        {"bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 4, "type": "VEC3"},
        {"bufferView": 0, "byteOffset": 24, "componentType": 5126, "count": 4, "type": "VEC2"},
        {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"},
        {"bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR"},
    ],
    "images": [{"name": "Checker", "uri": "texture.png"}],
    "samplers": [{"name": "LinearRepeat", "magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
    "textures": [{"name": "CheckerTexture", "source": 0, "sampler": 0}],
    "materials": [
        {"name": "RedChecker", "pbrMetallicRoughness": {"baseColorFactor": [1, 0.5, 0.5, 1], "baseColorTexture": {"index": 0}, "metallicFactor": 0.0, "roughnessFactor": 0.8}},
        {"name": "Blue", "pbrMetallicRoughness": {"baseColorFactor": [0.2, 0.3, 1.0, 1], "metallicFactor": 0.1, "roughnessFactor": 0.4}, "doubleSided": True},
    ],
    "meshes": [{"name": "TwoPrimitiveQuad", "primitives": [
        {"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0},
        {"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 4, "material": 1},
    ]}],
    "cameras": [{"name": "FixtureCamera", "type": "perspective", "perspective": {"yfov": 0.8, "znear": 0.1, "zfar": 100.0, "aspectRatio": 1.7777778}}],
    "extensions": {"KHR_lights_punctual": {"lights": [{"name": "Key", "type": "directional", "color": [1, 0.95, 0.9], "intensity": 3.0}]}},
    "nodes": [
        {"name": "NegativeScaleRoot", "mesh": 0, "scale": [-1, 1, 1], "children": [1]},
        {"name": "CameraChild", "camera": 0, "translation": [0, 0, 5]},
        {"name": "LightRoot", "extensions": {"KHR_lights_punctual": {"light": 0}}, "rotation": [0, 0, 0, 1]},
    ],
    "scenes": [{"name": "Main", "nodes": [0, 2]}, {"name": "LightOnly", "nodes": [2]}],
    "scene": 0,
}
(VALID / "external_scene.gltf").write_text(json.dumps(external_doc, indent=2), encoding="utf-8")

# Embedded image GLB. It includes TEXCOORD_0 because the material references a texture.
embedded_png = png_rgba(1, 1, bytes([255, 255, 0, 255]))
embedded_geometry = struct.pack("<9f3H", 0.0, 0.5, 0.0, 0.5, -0.5, 0.0, -0.5, -0.5, 0.0, 0, 1, 2)
embedded_uv = struct.pack("<6f", 0.5, 0.0, 1.0, 1.0, 0.0, 1.0)
uv_offset = len(align4(embedded_geometry))
image_offset = len(align4(align4(embedded_geometry) + embedded_uv))
embedded_bin = align4(align4(embedded_geometry) + embedded_uv) + embedded_png
embedded_doc = {
    "asset": {"version": "2.0", "generator": "Daedalus fixture generator"},
    "buffers": [{"byteLength": len(embedded_bin)}],
    "bufferViews": [
        {"buffer": 0, "byteOffset": 0, "byteLength": 36},
        {"buffer": 0, "byteOffset": 36, "byteLength": 6},
        {"buffer": 0, "byteOffset": uv_offset, "byteLength": len(embedded_uv)},
        {"buffer": 0, "byteOffset": image_offset, "byteLength": len(embedded_png)},
    ],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
        {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"},
        {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
    ],
    "images": [{"name": "EmbeddedYellow", "bufferView": 3, "mimeType": "image/png"}],
    "textures": [{"source": 0}],
    "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
    "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "TEXCOORD_0": 2}, "indices": 1, "material": 0}]}],
    "nodes": [{"mesh": 0}],
    "scenes": [{"nodes": [0]}],
}
write_glb(VALID / "embedded_image.glb", embedded_doc, embedded_bin)


# Data-URI fixture exercising normalized integer attributes, all diagnostic material metadata,
# an orthographic camera, an explicit sampler, and a data-URI PNG.
parts: list[bytes] = []
views: list[dict] = []
def append_view(payload: bytes, *, stride: int | None = None) -> int:
    offset = sum(len(part) for part in parts)
    padding = (4 - offset % 4) % 4
    if padding:
        parts.append(b"\x00" * padding)
        offset += padding
    parts.append(payload)
    view = {"buffer": 0, "byteOffset": offset, "byteLength": len(payload)}
    if stride is not None:
        view["byteStride"] = stride
    views.append(view)
    return len(views) - 1

position_view = append_view(struct.pack("<9f", -0.5, -0.5, 0.0, 0.5, -0.5, 0.0, 0.0, 0.5, 0.0))
normal_view = append_view(struct.pack("<9b", 0, 0, 127, 0, 0, 127, 0, 0, 127))
tangent_view = append_view(struct.pack("<12h", 32767, 0, 0, 32767, 32767, 0, 0, -32767, 32767, 0, 0, 32767))
uv_view = append_view(struct.pack("<6H", 0, 65535, 65535, 65535, 32768, 0))
color_view = append_view(bytes([255, 0, 0, 0, 255, 0, 0, 0, 255]))
index_view = append_view(bytes([0, 1, 2]))
data_blob = b"".join(parts)
data_png = png_rgba(1, 1, bytes([180, 120, 255, 255]))
data_png_linear = png_rgba(1, 1, bytes([128, 128, 255, 255]))
data_uri_doc = {
    "asset": {"version": "2.0", "generator": "Daedalus fixture generator"},
    "extensionsUsed": ["KHR_mesh_quantization"],
    "extensionsRequired": ["KHR_mesh_quantization"],
    "buffers": [{"uri": data_uri(data_blob), "byteLength": len(data_blob)}],
    "bufferViews": views,
    "accessors": [
        {"bufferView": position_view, "componentType": 5126, "count": 3, "type": "VEC3"},
        {"bufferView": normal_view, "componentType": 5120, "normalized": True, "count": 3, "type": "VEC3"},
        {"bufferView": tangent_view, "componentType": 5122, "normalized": True, "count": 3, "type": "VEC4"},
        {"bufferView": uv_view, "componentType": 5123, "normalized": True, "count": 3, "type": "VEC2"},
        {"bufferView": color_view, "componentType": 5121, "normalized": True, "count": 3, "type": "VEC3"},
        {"bufferView": index_view, "componentType": 5121, "count": 3, "type": "SCALAR"},
    ],
    "images": [
        {"name": "DataColorImage", "uri": data_uri(data_png, "image/png")},
        {"name": "DataLinearImage", "uri": data_uri(data_png_linear, "image/png")},
    ],
    "samplers": [
        {"name": "NearestClamp", "magFilter": 9728, "minFilter": 9728, "wrapS": 33071, "wrapT": 33648},
        {"name": "LinearRepeat", "magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497},
    ],
    "textures": [
        {"name": "DataColorTexture", "source": 0, "sampler": 0},
        {"name": "DataLinearTexture", "source": 1, "sampler": 1},
    ],
    "materials": [{
        "name": "MetadataExercise",
        "pbrMetallicRoughness": {
            "baseColorFactor": [0.8, 0.7, 0.6, 0.5],
            "baseColorTexture": {"index": 0, "texCoord": 0},
            "metallicFactor": 0.25,
            "roughnessFactor": 0.75,
            "metallicRoughnessTexture": {"index": 1, "texCoord": 0},
        },
        "normalTexture": {"index": 1, "texCoord": 0, "scale": 0.5},
        "occlusionTexture": {"index": 1, "texCoord": 0, "strength": 0.6},
        "emissiveTexture": {"index": 0, "texCoord": 0},
        "emissiveFactor": [0.1, 0.2, 0.3],
        "alphaMode": "MASK",
        "alphaCutoff": 0.4,
        "doubleSided": True,
    }],
    "meshes": [{"name": "NormalizedTriangle", "primitives": [{
        "attributes": {"POSITION": 0, "NORMAL": 1, "TANGENT": 2, "TEXCOORD_0": 3, "COLOR_0": 4},
        "indices": 5, "material": 0,
    }]}],
    "cameras": [{"name": "Ortho", "type": "orthographic", "orthographic": {"xmag": 2.0, "ymag": 1.0, "znear": 0.1, "zfar": 10.0}}],
    "nodes": [{"name": "NormalizedNode", "mesh": 0, "camera": 0}],
    "scenes": [{"name": "DataUri", "nodes": [0]}],
    "scene": 0,
}
(VALID / "data_uri_scene.gltf").write_text(json.dumps(data_uri_doc, indent=2), encoding="utf-8")


# Data-URI JPEG fixture exercises the declared JPEG subset without external files.
jpeg_doc = {
    "asset": {"version": "2.0", "generator": "Daedalus fixture generator"},
    "images": [{"name": "EmbeddedBlueJpeg", "uri": data_uri(JPEG_1X1_RGB, "image/jpeg")}],
    "textures": [{"source": 0}],
    "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
}
(VALID / "jpeg_image.gltf").write_text(json.dumps(jpeg_doc, indent=2), encoding="utf-8")


# Audit-closure fixture: explicit material 0 plus a primitive using the glTF default material.
material_blob = struct.pack("<9f3H", -0.5, -0.5, 0.0, 0.5, -0.5, 0.0, 0.0, 0.5, 0.0, 0, 1, 2)
material_default_doc = {
    "asset": {"version": "2.0", "generator": "Daedalus fixture generator"},
    "buffers": [{"uri": data_uri(material_blob), "byteLength": len(material_blob)}],
    "bufferViews": [
        {"buffer": 0, "byteOffset": 0, "byteLength": 36},
        {"buffer": 0, "byteOffset": 36, "byteLength": 6},
    ],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
        {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"},
    ],
    "materials": [{"name": "ExplicitRed", "pbrMetallicRoughness": {"baseColorFactor": [1, 0, 0, 1]}}],
    "meshes": [{"name": "DefaultMaterialProof", "primitives": [
        {"attributes": {"POSITION": 0}, "indices": 1, "material": 0},
        {"attributes": {"POSITION": 0}, "indices": 1},
    ]}],
    "nodes": [{"mesh": 0}],
    "scenes": [{"name": "DefaultMaterial", "nodes": [0]}],
    "scene": 0,
}
(VALID / "material_default.gltf").write_text(json.dumps(material_default_doc, indent=2), encoding="utf-8")

# Audit-closure fixture: UV0 collapses to one corner while UV1 spans a 2x2 checker.
uv1_positions = struct.pack("<9f", -0.8, -0.8, 0.0, 0.8, -0.8, 0.0, 0.0, 0.8, 0.0)
uv1_uv0 = struct.pack("<6f", 0.0, 0.0, 0.0, 0.0, 0.0, 0.0)
uv1_uv1 = struct.pack("<6f", 0.0, 1.0, 1.0, 1.0, 0.5, 0.0)
uv1_indices = struct.pack("<3H", 0, 1, 2)
uv1_blob = align4(uv1_positions) + align4(uv1_uv0) + align4(uv1_uv1) + uv1_indices
uv1_image = png_rgba(2, 2, bytes([
    255, 0, 0, 255, 0, 255, 0, 255,
    0, 0, 255, 255, 255, 255, 0, 255,
]))
uv1_doc = {
    "asset": {"version": "2.0", "generator": "Daedalus fixture generator"},
    "buffers": [{"uri": data_uri(uv1_blob), "byteLength": len(uv1_blob)}],
    "bufferViews": [
        {"buffer": 0, "byteOffset": 0, "byteLength": 36},
        {"buffer": 0, "byteOffset": 36, "byteLength": 24},
        {"buffer": 0, "byteOffset": 60, "byteLength": 24},
        {"buffer": 0, "byteOffset": 84, "byteLength": 6},
    ],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
        {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC2"},
        {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
        {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"},
    ],
    "images": [{"uri": data_uri(uv1_image, "image/png")}],
    "textures": [{"source": 0}],
    "materials": [{"name": "UV1Checker", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0, "texCoord": 1}}}],
    "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "TEXCOORD_0": 1, "TEXCOORD_1": 2}, "indices": 3, "material": 0}]}],
    "nodes": [{"mesh": 0}], "scenes": [{"name": "UV1", "nodes": [0]}], "scene": 0,
}
(VALID / "uv1_scene.gltf").write_text(json.dumps(uv1_doc, indent=2), encoding="utf-8")
missing_uv1_doc = json.loads(json.dumps(uv1_doc))
del missing_uv1_doc["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_1"]
(INVALID / "missing_uv1.gltf").write_text(json.dumps(missing_uv1_doc, indent=2), encoding="utf-8")

# Audit-closure fixture: one mesh instanced by two nodes, with tangents of both signs.
instance_positions = struct.pack("<9f", -0.4, -0.4, 0.0, 0.4, -0.4, 0.0, 0.0, 0.4, 0.0)
instance_normals = struct.pack("<9f", *( [0.0, 0.0, 1.0] * 3 ))
instance_tangents = struct.pack("<12f", 1.0,0.0,0.0,1.0, 0.0,1.0,0.0,-1.0, -1.0,0.0,0.0,1.0)
instance_indices = struct.pack("<3H", 0,1,2)
instance_blob = align4(instance_positions) + align4(instance_normals) + align4(instance_tangents) + instance_indices
instance_doc = {
    "asset": {"version": "2.0", "generator": "Daedalus fixture generator"},
    "buffers": [{"uri": data_uri(instance_blob), "byteLength": len(instance_blob)}],
    "bufferViews": [
        {"buffer":0,"byteOffset":0,"byteLength":36},
        {"buffer":0,"byteOffset":36,"byteLength":36},
        {"buffer":0,"byteOffset":72,"byteLength":48},
        {"buffer":0,"byteOffset":120,"byteLength":6},
    ],
    "accessors": [
        {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
        {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
        {"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},
        {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"},
    ],
    "meshes": [{"name":"InstancedTangentMesh","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TANGENT":2},"indices":3}]}],
    "nodes": [
        {"name":"LeftInstance","mesh":0,"translation":[-1.0,0.0,0.0]},
        {"name":"RightNegativeInstance","mesh":0,"translation":[1.0,0.0,0.0],"scale":[-1.0,1.0,1.0]},
    ],
    "scenes": [{"name":"Instances","nodes":[0,1]}], "scene":0,
}
(VALID / "instanced_tangents.gltf").write_text(json.dumps(instance_doc, indent=2), encoding="utf-8")

# Slightly non-unit values are repaired and reported.
repair_blob = struct.pack("<9f9f12f3H",
    -0.4,-0.4,0.0, 0.4,-0.4,0.0, 0.0,0.4,0.0,
    0.0,0.0,1.005, 0.0,0.0,1.005, 0.0,0.0,1.005,
    0.995,0.0,0.0,1.0, 0.995,0.0,0.0,-1.0, 0.995,0.0,0.0,1.0,
    0,1,2)
repair_doc = {
    "asset":{"version":"2.0"},
    "buffers":[{"uri":data_uri(repair_blob),"byteLength":len(repair_blob)}],
    "bufferViews":[
        {"buffer":0,"byteOffset":0,"byteLength":36},
        {"buffer":0,"byteOffset":36,"byteLength":36},
        {"buffer":0,"byteOffset":72,"byteLength":48},
        {"buffer":0,"byteOffset":120,"byteLength":6},
    ],
    "accessors":[
        {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
        {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
        {"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},
        {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"},
    ],
    "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TANGENT":2},"indices":3}]}],
    "nodes":[{"mesh":0,"rotation":[0,0,0,1.005]}],"scenes":[{"nodes":[0]}],
}
(VALID / "repaired_vectors.gltf").write_text(json.dumps(repair_doc, indent=2), encoding="utf-8")

# Structurally valid PNG chunks with invalid compressed pixel entropy.
def png_with_bad_entropy() -> bytes:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", b"not-a-zlib-stream") + chunk(b"IEND", b""))
(INVALID / "corrupt_entropy_png.gltf").write_text(json.dumps({
    "asset":{"version":"2.0"}, "images":[{"uri":data_uri(png_with_bad_entropy(), "image/png")}]
}), encoding="utf-8")

# Structurally plausible JPEG with a valid SOF/SOS envelope but corrupt scan entropy.
# Superficial marker/header inspection can accept this; the real decoder must reject it.
jpeg_sos = JPEG_1X1_RGB.find(b"\xff\xda")
assert jpeg_sos >= 0
jpeg_sos_length = int.from_bytes(JPEG_1X1_RGB[jpeg_sos + 2:jpeg_sos + 4], "big")
jpeg_scan_start = jpeg_sos + 2 + jpeg_sos_length
corrupt_entropy_jpeg = JPEG_1X1_RGB[:jpeg_scan_start] + b"garbage" + b"\xff\xd9"
(INVALID / "corrupt_entropy_jpeg.gltf").write_text(json.dumps({
    "asset":{"version":"2.0"}, "images":[{"uri":data_uri(corrupt_entropy_jpeg, "image/jpeg")}]
}), encoding="utf-8")

# Zero-length vector/quaternion rejection fixtures.
def vector_failure_doc(attribute_name: str, attribute_components: int, values: tuple[float, ...]) -> dict:
    positions = struct.pack("<9f", -0.5,-0.5,0.0, 0.5,-0.5,0.0, 0.0,0.5,0.0)
    attribute = struct.pack("<" + "f" * len(values), *values)
    indices = struct.pack("<3H", 0,1,2)
    blob = align4(positions) + align4(attribute) + indices
    return {
        "asset":{"version":"2.0"}, "buffers":[{"uri":data_uri(blob),"byteLength":len(blob)}],
        "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}, {"buffer":0,"byteOffset":36,"byteLength":len(attribute)}, {"buffer":0,"byteOffset":36+len(align4(attribute)),"byteLength":6}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}, {"bufferView":1,"componentType":5126,"count":3,"type":f"VEC{attribute_components}"}, {"bufferView":2,"componentType":5123,"count":3,"type":"SCALAR"}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0,attribute_name:1},"indices":2}]}], "nodes":[{"mesh":0}], "scenes":[{"nodes":[0]}],
    }
(INVALID / "zero_normal.gltf").write_text(json.dumps(vector_failure_doc("NORMAL", 3, (0.0,0.0,0.0)*3)), encoding="utf-8")
(INVALID / "zero_tangent.gltf").write_text(json.dumps(vector_failure_doc("TANGENT", 4, (0.0,0.0,0.0,1.0)*3)), encoding="utf-8")
(INVALID / "zero_quaternion.gltf").write_text(json.dumps({"asset":{"version":"2.0"},"nodes":[{"rotation":[0,0,0,0]}],"scenes":[{"nodes":[0]}]}), encoding="utf-8")

# Invalid fixtures.
(INVALID / "malformed_json.gltf").write_text('{"asset":{"version":"2.0"},', encoding="utf-8")
(INVALID / "corrupted_header.glb").write_bytes(struct.pack("<III", 0x46546C67, 2, 999) + b"broken")

missing_buffer = {"asset": {"version": "2.0"}, "buffers": [{"uri": "does-not-exist.bin", "byteLength": 12}]}
(INVALID / "missing_buffer.gltf").write_text(json.dumps(missing_buffer), encoding="utf-8")
missing_image = {"asset": {"version": "2.0"}, "images": [{"uri": "does-not-exist.png"}]}
(INVALID / "missing_image.gltf").write_text(json.dumps(missing_image), encoding="utf-8")

base_positions = struct.pack("<9f3H", 0.0, 0.5, 0.0, 0.5, -0.5, 0.0, -0.5, -0.5, 0.0, 0, 1, 2)

def simple_doc(blob: bytes) -> dict:
    return {
        "asset": {"version": "2.0"},
        "buffers": [{"uri": data_uri(blob), "byteLength": len(blob)}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36}, {"buffer": 0, "byteOffset": 36, "byteLength": max(0, len(blob) - 36)}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"}, {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
        "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}],
    }

# Valid but degraded: declared POSITION bounds are intentionally stale. The importer must
# retain the decoded geometry, report a deterministic repair, and use recomputed bounds.
stale_bounds = simple_doc(base_positions)
stale_bounds["accessors"][0]["min"] = [-10.0, -10.0, -10.0]
stale_bounds["accessors"][0]["max"] = [10.0, 10.0, 10.0]
(VALID / "stale_bounds.gltf").write_text(json.dumps(stale_bounds, indent=2), encoding="utf-8")

accessor_oob = simple_doc(base_positions)
accessor_oob["accessors"][0]["count"] = 100
(INVALID / "accessor_oob.gltf").write_text(json.dumps(accessor_oob), encoding="utf-8")

# Huge accessor count exercises checked arithmetic before any allocation or buffer access.
budget_overflow = simple_doc(base_positions)
budget_overflow["accessors"][0]["count"] = 2_000_000_000_000_000_000
(INVALID / "budget_overflow.gltf").write_text(json.dumps(budget_overflow), encoding="utf-8")

invalid_stride = simple_doc(base_positions)
invalid_stride["bufferViews"][0]["byteStride"] = 4
(INVALID / "invalid_stride.gltf").write_text(json.dumps(invalid_stride), encoding="utf-8")

index_oob_blob = struct.pack("<9f3H", 0.0, 0.5, 0.0, 0.5, -0.5, 0.0, -0.5, -0.5, 0.0, 0, 1, 9)
(INVALID / "index_oob.gltf").write_text(json.dumps(simple_doc(index_oob_blob)), encoding="utf-8")

nan_blob = struct.pack("<9f3H", math.nan, 0.5, 0.0, 0.5, -0.5, 0.0, -0.5, -0.5, 0.0, 0, 1, 2)
(INVALID / "nonfinite_attribute.gltf").write_text(json.dumps(simple_doc(nan_blob)), encoding="utf-8")

unsupported_required = {"asset": {"version": "2.0"}, "extensionsUsed": ["EXT_not_real"], "extensionsRequired": ["EXT_not_real"]}
(INVALID / "unsupported_required_extension.gltf").write_text(json.dumps(unsupported_required), encoding="utf-8")

unsupported_mode = simple_doc(base_positions)
unsupported_mode["meshes"][0]["primitives"][0]["mode"] = 1
(INVALID / "unsupported_primitive_mode.gltf").write_text(json.dumps(unsupported_mode), encoding="utf-8")

invalid_parent = {"asset": {"version": "2.0"}, "nodes": [{"children": [2]}, {"children": [2]}, {}], "scenes": [{"nodes": [0, 1]}]}
(INVALID / "invalid_parent.gltf").write_text(json.dumps(invalid_parent), encoding="utf-8")

unsupported_image = {"asset": {"version": "2.0"}, "images": [{"uri": data_uri(b"GIF89a" + b"\x00" * 20, "image/gif")}]}
(INVALID / "unsupported_image.gltf").write_text(json.dumps(unsupported_image), encoding="utf-8")

truncated_png = png_rgba(1, 1, bytes([255, 0, 255, 255]))[:-12]
(INVALID / "truncated_png.gltf").write_text(json.dumps({
    "asset": {"version": "2.0"}, "images": [{"uri": data_uri(truncated_png, "image/png")}]
}), encoding="utf-8")

invalid_base64 = {"asset": {"version": "2.0"}, "buffers": [{"uri": "data:application/octet-stream;base64,A", "byteLength": 1}]}
(INVALID / "invalid_base64.gltf").write_text(json.dumps(invalid_base64), encoding="utf-8")

network_uri = {"asset": {"version": "2.0"}, "buffers": [{"uri": "https://example.invalid/asset.bin", "byteLength": 4}]}
(INVALID / "network_uri.gltf").write_text(json.dumps(network_uri), encoding="utf-8")

path_traversal = {"asset": {"version": "2.0"}, "buffers": [{"uri": "../outside.bin", "byteLength": 4}]}
(INVALID / "path_traversal.gltf").write_text(json.dumps(path_traversal), encoding="utf-8")

material_out_of_range = {"asset": {"version": "2.0"}, "materials": [{"pbrMetallicRoughness": {"metallicFactor": 1.5}}]}
(INVALID / "material_out_of_range.gltf").write_text(json.dumps(material_out_of_range), encoding="utf-8")

quantization_not_required = {"asset": {"version": "2.0"}, "extensionsUsed": ["KHR_mesh_quantization"]}
(INVALID / "quantization_not_required.gltf").write_text(json.dumps(quantization_not_required), encoding="utf-8")

manifest = {
    "license": "CC0-1.0; all fixtures are generated and self-authored",
    "valid": [
        "minimal.glb", "external_scene.gltf", "embedded_image.glb", "data_uri_scene.gltf",
        "jpeg_image.gltf", "stale_bounds.gltf", "material_default.gltf", "uv1_scene.gltf",
        "instanced_tangents.gltf", "repaired_vectors.gltf"
    ],
    "invalid": [
        "malformed_json.gltf", "corrupted_header.glb", "missing_buffer.gltf", "missing_image.gltf",
        "accessor_oob.gltf", "invalid_stride.gltf", "index_oob.gltf", "nonfinite_attribute.gltf",
        "unsupported_required_extension.gltf", "unsupported_primitive_mode.gltf", "invalid_parent.gltf",
        "unsupported_image.gltf", "truncated_png.gltf", "invalid_base64.gltf", "network_uri.gltf",
        "path_traversal.gltf", "material_out_of_range.gltf",
        "quantization_not_required.gltf", "missing_uv1.gltf", "corrupt_entropy_png.gltf",
        "corrupt_entropy_jpeg.gltf", "zero_normal.gltf", "zero_tangent.gltf", "zero_quaternion.gltf", "budget_overflow.gltf",
    ],
}
(ROOT / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
print("Generated Campaign B fixtures")
