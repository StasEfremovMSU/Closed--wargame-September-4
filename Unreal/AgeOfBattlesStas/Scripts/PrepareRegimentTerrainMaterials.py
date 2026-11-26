"""Generate the shared, opaque terrain and river materials for RegimentField.

Run via UnrealEditor-Cmd -run=pythonscript -script=<this file>.
"""

import unreal


DIRECTORY = "/Game/Battle/Materials/Terrain"
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EDITOR = unreal.MaterialEditingLibrary


GROUND_COLOR = """
float2 P = (World.xy - Origin.xy) * PatternScale;
float2 Cell = floor(P);
float2 Weight = frac(P);
Weight = Weight * Weight * (3.0 - 2.0 * Weight);
float4 X = Cell.x + float4(0.0, 1.0, 0.0, 1.0);
float4 Y = Cell.y + float4(0.0, 0.0, 1.0, 1.0);
float4 Seed = frac(X * 0.1031 + Y * 0.11369);
Seed *= Seed + 19.19;
float4 Shade = frac(Seed * (Seed + 4.77));
float Broad = lerp(lerp(Shade.x, Shade.y, Weight.x),
                   lerp(Shade.z, Shade.w, Weight.x), Weight.y);
float Slope = 1.0 - abs(Normal.z);
float Height = saturate((World.z - Origin.z) * 0.0011);
float Exposed = saturate(Broad * 0.74
                 + Relief * (Slope * 5.8 + Height * 0.39));
return lerp(Color, EarthTone, Exposed) * (0.93 + 0.14 * Broad);
"""

RIVER_SURFACE = """
float2 Across = float2(-FlowDirection.y, FlowDirection.x);
float2 P = World.xy - Origin.xy;
float U = dot(P, Across);
float V = dot(P, FlowDirection.xy) - Time * Speed;
float Bend = sin(U * 0.014 + V * 0.0017);
float Swell = 0.5 + 0.5 * sin(V * 0.018 + U * 0.043 + Bend);
float Ripples = 0.5 + 0.5 * sin(U * 0.076 - V * 0.008 + Bend);
float Silver = smoothstep(0.75, 0.98, Swell * 0.72 + Ripples * 0.28);
return float4(lerp(Color, Sheen, Swell * 0.19 + Silver * 0.42),
              lerp(0.42, 0.25, Silver));
"""


def expression(material, cls, x, y, **properties):
    node = EDITOR.create_material_expression(material, cls, x, y)
    for name, value in properties.items():
        node.set_editor_property(name, value)
    return node


def vector(material, name, value, y):
    return expression(material, unreal.MaterialExpressionVectorParameter, -620, y,
                      parameter_name=name, default_value=unreal.LinearColor(*value, 1.0))


def scalar(material, name, value, y):
    return expression(material, unreal.MaterialExpressionScalarParameter, -620, y,
                      parameter_name=name, default_value=value)


def custom(material, code, output, nodes):
    inputs = []
    for name, _, _ in nodes:
        input_pin = unreal.CustomInput()
        input_pin.set_editor_property("input_name", name)
        inputs.append(input_pin)
    result = expression(material, unreal.MaterialExpressionCustom, -250, 0,
                        code=code, output_type=output, inputs=inputs)
    for name, source, output_name in nodes:
        if not EDITOR.connect_material_expressions(source, output_name, result, name):
            raise RuntimeError(f"Could not connect {name} on {material.get_name()}")
    return result


def connect(source, prop):
    if not EDITOR.connect_material_property(source, "", prop):
        raise RuntimeError(f"Could not connect {prop} on {source.get_outer().get_name()}")


def ground(material):
    value = custom(material, GROUND_COLOR, unreal.CustomMaterialOutputType.CMOT_FLOAT3, [
        ("World", expression(material, unreal.MaterialExpressionWorldPosition, -620, 0), "XYZ"),
        ("Normal", expression(material, unreal.MaterialExpressionVertexNormalWS, -620, 110), ""),
        ("Origin", vector(material, "BoardOrigin", (0., 0., 0.), 210), "RGB"),
        ("Color", vector(material, "Color", (.25, .29, .15), 345), "RGB"),
        ("EarthTone", vector(material, "EarthTone", (.33, .27, .17), 485), "RGB"),
        ("PatternScale", scalar(material, "PatternScale", .00064, 620), ""),
        ("Relief", scalar(material, "ReliefStrength", .65, 715), ""),
    ])
    connect(value, unreal.MaterialProperty.MP_BASE_COLOR)
    connect(scalar(material, "Roughness", .89, 820), unreal.MaterialProperty.MP_ROUGHNESS)
    connect(scalar(material, "Specular", .18, 920), unreal.MaterialProperty.MP_SPECULAR)


def river(material):
    value = custom(material, RIVER_SURFACE, unreal.CustomMaterialOutputType.CMOT_FLOAT4, [
        ("World", expression(material, unreal.MaterialExpressionWorldPosition, -620, 0), "XYZ"),
        ("Origin", vector(material, "BoardOrigin", (0., 0., 0.), 110), "RGB"),
        ("Color", vector(material, "Color", (.05, .13, .17), 250), "RGB"),
        ("Sheen", vector(material, "Sheen", (.18, .35, .38), 395), "RGB"),
        ("FlowDirection", vector(material, "FlowDirection", (0., 1., 0.), 535), "RGB"),
        ("Time", expression(material, unreal.MaterialExpressionTime, -620, 680), ""),
        ("Speed", scalar(material, "Speed", 63., 785), ""),
    ])
    rgb = expression(material, unreal.MaterialExpressionComponentMask, 0, -25, r=True, g=True, b=True)
    alpha = expression(material, unreal.MaterialExpressionComponentMask, 0, 175, a=True)
    for mask in (rgb, alpha):
        if not EDITOR.connect_material_expressions(value, "", mask, ""):
            raise RuntimeError("Could not split river color and roughness")
    connect(rgb, unreal.MaterialProperty.MP_BASE_COLOR)
    connect(alpha, unreal.MaterialProperty.MP_ROUGHNESS)


def prepare(name, build, base_color_code=None):
    path = f"{DIRECTORY}/{name}"
    if ASSETS.does_asset_exist(path):
        if base_color_code is not None:
            material = ASSETS.load_asset(path)
            node = EDITOR.get_material_property_input_node(material, unreal.MaterialProperty.MP_BASE_COLOR)
            if not isinstance(node, unreal.MaterialExpressionCustom):
                raise RuntimeError(f"Expected the terrain color expression on {path}")
            if node.get_editor_property("code") != base_color_code:
                node.set_editor_property("code", base_color_code)
                EDITOR.recompile_material(material)
                if not ASSETS.save_loaded_asset(material):
                    raise RuntimeError(f"Could not save {path}")
        return

    material = TOOLS.create_asset(name, DIRECTORY, unreal.Material, unreal.MaterialFactoryNew())
    if not material:
        raise RuntimeError(f"Could not create {path}")
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    build(material)
    usage = unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES
    EDITOR.set_base_material_usage(material, usage)
    if not EDITOR.has_material_usage(material, usage):
        raise RuntimeError(f"Could not enable instanced mesh usage on {path}")
    EDITOR.recompile_material(material)
    if not ASSETS.save_loaded_asset(material):
        raise RuntimeError(f"Could not save {path}")
    unreal.log(f"Prepared opaque battlefield material {path}")


if __name__ == "__main__":
    prepare("M_RegimentTerrain", ground, GROUND_COLOR)
    prepare("M_RegimentRiver", river)
