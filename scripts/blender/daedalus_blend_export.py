"""Daedalus controlled Blender -> GLB conversion script (Campaign H).

Run ONLY by the Daedalus BlenderWorker through a separately installed Blender:

    blender --background --factory-startup --python-exit-code 3 \
        --python daedalus_blend_export.py -- <source.blend> <output.glb> <manifest.json>

Daedalus never parses .blend files itself. This script opens the .blend inside Blender, records
what the glTF exporter cannot represent, exports a self-contained GLB (images embedded) and writes a
JSON manifest that the C++ worker parses. The manifest is written even when the export fails so the
failure is diagnosable.

Versioning: SCRIPT_VERSION must equal kBlenderExportScriptVersion in src/assets/BlenderWorker.h.
Any change to the export arguments or manifest content must bump it, because the asset cache key
includes it.

This file is repository-owned source; it has not been executed as part of the D-H source drop.
"""

import json
import os
import sys
import traceback

import bpy  # noqa: provided by Blender

SCRIPT_VERSION = "daedalus-blend-export/1"
MANIFEST_SCHEMA = "daedalus.blender-conversion-manifest/1"

# Shader nodes the stock glTF exporter understands (directly or as recognised patterns). Anything
# else in a material's node tree is reported as unsupported; the exporter silently drops it.
SUPPORTED_SHADER_NODES = {
    "OUTPUT_MATERIAL",
    "BSDF_PRINCIPLED",
    "EMISSION",
    "BSDF_DIFFUSE",
    "BSDF_TRANSPARENT",
    "MIX_SHADER",
    "ADD_SHADER",
    "TEX_IMAGE",
    "NORMAL_MAP",
    "UVMAP",
    "TEX_COORD",
    "MAPPING",
    "SEPARATE_COLOR",
    "SEPARATE_RGB",
    "COMBINE_COLOR",
    "COMBINE_RGB",
    "VERTEX_COLOR",
    "ATTRIBUTE",
    "MIX",
    "MIX_RGB",
    "MATH",
    "VALUE",
    "RGB",
    "GROUP",
    "REROUTE",
    "FRAME",
    "GROUP_INPUT",
    "GROUP_OUTPUT",
}

EXPORTABLE_OBJECT_TYPES = {"MESH", "EMPTY", "CAMERA", "LIGHT", "ARMATURE"}
CONVERTIBLE_OBJECT_TYPES = {"CURVE", "SURFACE", "META", "FONT"}


def parse_arguments():
    if "--" not in sys.argv:
        raise SystemExit("expected '-- <source.blend> <output.glb> <manifest.json>'")
    arguments = sys.argv[sys.argv.index("--") + 1:]
    if len(arguments) != 3:
        raise SystemExit("expected exactly three arguments after '--'")
    return [os.path.abspath(argument) for argument in arguments]


def inspect_materials(manifest):
    for material in bpy.data.materials:
        if material.users == 0:
            continue
        if not material.use_nodes or material.node_tree is None:
            manifest["approximations"].append(
                "material '%s' does not use nodes; exported from viewport colour only" % material.name)
            continue
        unsupported = sorted({node.type for node in material.node_tree.nodes if node.type not in SUPPORTED_SHADER_NODES})
        if unsupported:
            manifest["unsupported"].append(
                "material '%s' uses shader nodes the glTF exporter discards: %s" % (material.name, ", ".join(unsupported)))
        if any(node.type == "MIX_SHADER" for node in material.node_tree.nodes):
            manifest["approximations"].append(
                "material '%s' mixes shaders; the exporter keeps only recognised mix patterns" % material.name)
        blend_method = getattr(material, "blend_method", "OPAQUE")
        if blend_method not in ("OPAQUE", "CLIP", "BLEND", "HASHED"):
            manifest["approximations"].append("material '%s' blend method %s" % (material.name, blend_method))


def inspect_images(manifest):
    for image in bpy.data.images:
        if image.users == 0 or image.type not in ("IMAGE",):
            continue
        if image.packed_file is not None:
            manifest["packed_images"].append(image.name)
            continue
        if image.source in ("SEQUENCE", "MOVIE", "TILED"):
            manifest["unsupported"].append("image '%s' has source %s (only still images export)" % (image.name, image.source))
            continue
        path = bpy.path.abspath(image.filepath)
        if not path or not os.path.isfile(path):
            manifest["warnings"].append("image '%s' file is missing: %s" % (image.name, image.filepath))
        else:
            manifest["external_images"].append(os.path.normpath(path))


def inspect_objects(manifest):
    for obj in bpy.context.scene.objects:
        if obj.type in CONVERTIBLE_OBJECT_TYPES:
            manifest["approximations"].append(
                "object '%s' of type %s is converted to a mesh by the exporter" % (obj.name, obj.type))
        elif obj.type not in EXPORTABLE_OBJECT_TYPES:
            manifest["unsupported"].append("object '%s' of type %s is not exported" % (obj.name, obj.type))
        if obj.type == "LIGHT":
            light_type = obj.data.type
            if light_type == "AREA":
                manifest["unsupported"].append(
                    "area light '%s' has no KHR_lights_punctual equivalent and is not exported" % obj.name)
            elif light_type == "SUN":
                manifest["approximations"].append(
                    "sun light '%s' exported as a directional light (angular diameter discarded)" % obj.name)
        for modifier in getattr(obj, "modifiers", []):
            if modifier.type == "PARTICLE_SYSTEM":
                manifest["unsupported"].append("particle system on '%s' is not exported" % obj.name)
            elif modifier.type == "NODES":
                manifest["approximations"].append(
                    "geometry nodes on '%s' are evaluated and exported as baked mesh data" % obj.name)
        if obj.animation_data is not None and obj.animation_data.action is not None:
            manifest["warnings"].append("animation on '%s' is not exported (static conversion)" % obj.name)


def record_units(manifest):
    unit_settings = bpy.context.scene.unit_settings
    manifest["source_units"] = {
        "system": unit_settings.system,
        "scale_length": float(unit_settings.scale_length),
        "length_unit": getattr(unit_settings, "length_unit", ""),
    }
    manifest["axis_conversion"] = "Blender +Z up -> glTF +Y up (exporter export_yup=True)"
    if abs(unit_settings.scale_length - 1.0) > 1.0e-6:
        manifest["approximations"].append(
            "scene unit scale %.6g is not applied; glTF output is in Blender units (1 BU = 1 m)" % unit_settings.scale_length)


def export_glb(output_path, manifest):
    # Arguments vary between Blender releases; try the most explicit set first and fall back by
    # dropping keywords an older/newer exporter rejects (unknown keywords raise TypeError).
    preferred = dict(
        filepath=output_path,
        export_format="GLB",
        export_yup=True,
        export_apply=True,
        export_texcoords=True,
        export_normals=True,
        export_tangents=True,
        export_materials="EXPORT",
        export_image_format="AUTO",
        export_cameras=True,
        export_lights=True,
        export_extras=False,
        export_animations=False,
        export_skins=False,
        export_morph=False,
        use_selection=False,
        use_visible=False,
        use_renderable=True,
    )
    optional_keys = ["use_renderable", "use_visible", "export_image_format", "export_morph", "export_skins",
                     "export_animations", "export_extras", "export_tangents"]
    arguments = dict(preferred)
    dropped = []
    while True:
        try:
            result = bpy.ops.export_scene.gltf(**arguments)
            break
        except TypeError as error:
            removable = [key for key in optional_keys if key in arguments]
            if not removable:
                raise
            key = removable[0]
            del arguments[key]
            dropped.append(key)
            manifest["warnings"].append("exporter rejected argument '%s' (%s); retried without it" % (key, error))
    manifest["export_arguments"] = {key: value for key, value in arguments.items() if key != "filepath"}
    manifest["dropped_export_arguments"] = dropped
    if "FINISHED" not in result:
        raise RuntimeError("glTF exporter returned %s" % sorted(result))


def main():
    source, output_path, manifest_path = parse_arguments()
    manifest = {
        "schema": MANIFEST_SCHEMA,
        "script_version": SCRIPT_VERSION,
        "blender_version": bpy.app.version_string,
        "source": source,
        "output": output_path,
        "warnings": [],
        "unsupported": [],
        "approximations": [],
        "packed_images": [],
        "external_images": [],
        "succeeded": False,
    }
    exit_code = 0
    try:
        bpy.ops.wm.open_mainfile(filepath=source, load_ui=False)
        try:
            exporter = bpy.context.preferences.addons.get("io_scene_gltf2")
            if exporter is not None and hasattr(exporter, "module"):
                manifest["exporter_module"] = exporter.module
        except Exception:  # preference introspection is informational only
            pass
        record_units(manifest)
        inspect_materials(manifest)
        inspect_images(manifest)
        inspect_objects(manifest)
        export_glb(output_path, manifest)
        manifest["succeeded"] = True
    except Exception as error:
        manifest["failure"] = "%s: %s" % (type(error).__name__, error)
        manifest["traceback"] = traceback.format_exc()
        exit_code = 2
    for key in ("warnings", "unsupported", "approximations", "packed_images", "external_images"):
        manifest[key] = sorted(set(manifest[key]))
    with open(manifest_path, "w", encoding="utf-8", newline="\n") as stream:
        json.dump(manifest, stream, indent=2, sort_keys=True)
        stream.write("\n")
    sys.stdout.flush()
    if exit_code != 0:
        sys.exit(exit_code)


if __name__ == "__main__":
    main()
