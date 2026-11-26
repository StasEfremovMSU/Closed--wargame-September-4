"""Prepare the RegimentField combat materials with Unreal's pythonscript commandlet."""

import math

import unreal


MATERIALS = "/Game/Battle/Materials"
PALETTE = "/Game/Fab/Free_LOWPO__Mercenary_Company___Low_Poly_Characters___Weapons/Texture"
TEAM_TINT = "TeamTint"
TEAM_TINT_STRENGTH = "TeamTintStrength"
FIGURE_GRAPH_VERSION = "[Regiment Figure Surfaces 1]"
PARTS_GRAPH_VERSION = "[Regiment Part Surfaces 1]"
SKELETAL = unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH
INSTANCED = unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES

assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor = unreal.MaterialEditingLibrary


def save_asset(asset):
    if not assets.save_loaded_asset(asset):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")


def load_or_create_material(name, asset_class, factory):
    path = f"{MATERIALS}/{name}"
    if assets.does_asset_exist(path):
        material = unreal.load_asset(path)
        if not isinstance(material, asset_class):
            raise RuntimeError(f"Unexpected asset type at {path}")
        return material, False

    material = asset_tools.create_asset(name, MATERIALS, asset_class, factory)
    if not material:
        raise RuntimeError(f"Could not create {path}")
    return material, True


def enable_usage(material, usage):
    if editor.has_material_usage(material, usage):
        return False
    editor.set_base_material_usage(material, usage)
    if not editor.has_material_usage(material, usage):
        raise RuntimeError(f"Could not enable {usage} on {material.get_path_name()}")
    return True


def parameter(material, kind, name, default, x, y, primitive_index=None):
    node = editor.create_material_expression(material, kind, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    if primitive_index is not None:
        node.set_editor_property("use_custom_primitive_data", True)
        node.set_editor_property("primitive_data_index", primitive_index)
    return node


def connect(source, output, target, name):
    if not editor.connect_material_expressions(source, output, target, name):
        raise RuntimeError(f"Could not connect {name} on {target.get_name()}")


def connect_property(source, output, prop):
    if not editor.connect_material_property(source, output, prop):
        raise RuntimeError(f"Could not connect {prop}")


def surface_expression(material, version, inputs, code):
    node = editor.create_material_expression(material, unreal.MaterialExpressionCustom, -240, 0)
    node.set_editor_property("description", version)
    node.set_editor_property("code", code)
    node.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom_inputs = []
    for name, _, _ in inputs:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", name)
        custom_inputs.append(entry)
    node.set_editor_property("inputs", custom_inputs)
    custom_outputs = []
    for name in ("SurfaceRoughness", "SurfaceMetallic"):
        entry = unreal.CustomOutput()
        entry.set_editor_property("output_name", name)
        entry.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        custom_outputs.append(entry)
    node.set_editor_property("additional_outputs", custom_outputs)
    for name, source, output in inputs:
        connect(source, output, node, name)
    connect_property(node, "", unreal.MaterialProperty.MP_BASE_COLOR)
    connect_property(node, "SurfaceRoughness", unreal.MaterialProperty.MP_ROUGHNESS)
    connect_property(node, "SurfaceMetallic", unreal.MaterialProperty.MP_METALLIC)


def create_figure_graph(material, palette):
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)

    texture = editor.create_material_expression(material, unreal.MaterialExpressionTextureSample, -580, -150)
    texture.set_editor_property("texture", palette)
    texture.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)

    uv = editor.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -780, -150)
    connect(uv, "", texture, "UVs")
    tint = parameter(material, unreal.MaterialExpressionVectorParameter, TEAM_TINT, unreal.LinearColor.WHITE, -580, 100)
    strength = parameter(material, unreal.MaterialExpressionScalarParameter, TEAM_TINT_STRENGTH, 0.0, -580, 260)
    skin = parameter(material, unreal.MaterialExpressionVectorParameter, "FigureSkin", unreal.LinearColor(0, 0, 0, 0),
                     -580, 420, 0)
    shades = parameter(material, unreal.MaterialExpressionVectorParameter, "FigureShades", unreal.LinearColor(1, 1, 0, 0),
                       -580, 600, 4)
    surface_expression(material, FIGURE_GRAPH_VERSION, (
        ("Palette", texture, "RGB"), ("UV", uv, ""), ("Tint", tint, "RGB"), ("Strength", strength, ""),
        ("Skin", skin, "RGB"), ("IsFigure", skin, "A"), ("Shades", shades, "RGB"),
    ), """
// LOWPO's existing 16x16 palette. Identify the authored swatches by UV so color
// compression, lighting and team colors cannot turn armor or straps into skin.
float2 cell = floor(frac(UV) * 16.0);
float x = cell.x;
float y = cell.y;
bool skin = x == 13 && y == 3 && IsFigure > 0.5;
bool leather = (x == 13 && y >= 10 && y <= 14) || (x == 14 && y == 12);
bool metal = y == 15 && x >= 6 && x <= 9;
bool wood = IsFigure < 0.5 && ((x == 13 && y == 7) || (x == 12 && y == 13));
bool eye = x == 15 && y == 15 && IsFigure > 0.5;
SurfaceRoughness = skin ? 0.68 : metal ? 0.57 : wood ? 0.79 : leather ? 0.74 : 0.91;
SurfaceMetallic = metal ? 0.30 : 0.0;
float teamWeight = metal ? Strength * 0.65 : leather ? Strength * 0.24 : Strength;
float scale = IsFigure < 0.5 ? 1.0 : leather ? Shades.y : Shades.x;
float3 color = lerp(Palette, Tint, teamWeight) * scale;
return skin ? Skin : eye ? Palette : color;
""")


def create_parts_graph(material):
    color = parameter(material, unreal.MaterialExpressionVectorParameter, "PartColor",
                      unreal.LinearColor(0.19, 0.15, 0.11, 1.0), -790, -130)
    profile = parameter(material, unreal.MaterialExpressionScalarParameter, "PartSurface", 0.0, -790, 100)
    instance_color = editor.create_material_expression(
        material, unreal.MaterialExpressionPerInstanceCustomData3Vector, -530, -130)
    instance_profile = editor.create_material_expression(
        material, unreal.MaterialExpressionPerInstanceCustomData, -530, 100)
    instance_profile.set_editor_property("data_index", 3)
    connect(color, "RGB", instance_color, "DefaultValue")
    connect(profile, "", instance_profile, "DefaultValue")
    surface_expression(material, PARTS_GRAPH_VERSION, (
        ("Color", instance_color, ""), ("Profile", instance_profile, ""),
    ), """
// Cloth, leather/hide, wood, and steel share one set of instance batches.
SurfaceRoughness = Profile < 0.5 ? 0.91 : Profile < 1.5 ? 0.74 : Profile < 2.5 ? 0.79 : 0.61;
SurfaceMetallic = Profile > 2.5 ? 0.27 : 0.0;
return Color;
""")


def prepare_master(palette):
    material, created = load_or_create_material("M_RegimentFigureSurfaces", unreal.Material, unreal.MaterialFactoryNew())
    if created:
        create_figure_graph(material, palette)

    enabled_skeletal = enable_usage(material, SKELETAL)
    enabled_instanced = enable_usage(material, INSTANCED)
    if created or enabled_skeletal or enabled_instanced:
        editor.recompile_material(material)
        save_asset(material)
    return material


def prepare_parts():
    material, created = load_or_create_material("M_RegimentFigureParts", unreal.Material, unreal.MaterialFactoryNew())
    if created:
        create_parts_graph(material)
    enabled_instanced = enable_usage(material, INSTANCED)
    if created or enabled_instanced:
        editor.recompile_material(material)
        save_asset(material)


def has_tint(instance, tint, strength):
    current_tint = editor.get_material_instance_vector_parameter_value(instance, TEAM_TINT)
    current_strength = editor.get_material_instance_scalar_parameter_value(instance, TEAM_TINT_STRENGTH)
    return current_tint == tint and math.isclose(current_strength, strength, abs_tol=1e-6)


def prepare_instance(name, parent, tint, strength):
    instance, created = load_or_create_material(
        name, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew()
    )
    parent_changed = instance.get_editor_property("parent") != parent
    if parent_changed:
        editor.set_material_instance_parent(instance, parent)

    if created or parent_changed or not has_tint(instance, tint, strength):
        # In UE 5.8 these setters return False even when they write the parameter.
        editor.set_material_instance_vector_parameter_value(instance, TEAM_TINT, tint)
        editor.set_material_instance_scalar_parameter_value(instance, TEAM_TINT_STRENGTH, strength)
        if not has_tint(instance, tint, strength):
            raise RuntimeError(f"Could not set the team tint on {name}")
        save_asset(instance)


def prepare_marker(team):
    destination = f"{MATERIALS}/M_RegimentMarker{team}"
    created = not assets.does_asset_exist(destination)
    if created:
        source = f"{MATERIALS}/M_Company{team}"
        marker = assets.duplicate_asset(source, destination)
    else:
        marker = unreal.load_asset(destination)
    if not isinstance(marker, unreal.Material):
        raise RuntimeError(f"Could not prepare company marker material {destination}")

    enabled_instanced = enable_usage(marker, INSTANCED)
    if created or enabled_instanced:
        editor.recompile_material(marker)
        save_asset(marker)


def main():
    palette = unreal.load_asset(PALETTE)
    if not isinstance(palette, unreal.Texture2D) or not palette.get_editor_property("srgb"):
        raise RuntimeError(f"Missing sRGB palette texture: {PALETTE}")

    parent = prepare_master(palette)
    prepare_parts()
    prepare_instance("MI_RegimentFigureBlue", parent, unreal.LinearColor(0.04, 0.25, 0.90, 1.0), 0.28)
    prepare_instance("MI_RegimentFigureOrange", parent, unreal.LinearColor(0.89, 0.23, 0.04, 1.0), 0.28)
    prepare_instance("MI_RegimentWeapon", parent, unreal.LinearColor.WHITE, 0.0)

    for team in ("Blue", "Orange"):
        prepare_marker(team)

    unreal.log(f"Regiment figure and company marker materials ready in {MATERIALS}")


if __name__ == "__main__":
    main()
