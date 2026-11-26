"""Generate the shared sky dome material using Unreal's built-in cloud mask."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))

import unreal

from PrepareRegimentTerrainMaterials import ASSETS, EDITOR, TOOLS, connect, custom, expression, vector


DIRECTORY = "/Game/Battle/Materials/Terrain"
NAME = "M_RegimentSkyDome"
CLOUD_MASK = "/Engine/EngineSky/T_Sky_Clouds_M"

SKY_COLOR = """
float3 Ray = normalize(-ViewDirection);
float Height = saturate(Ray.z);
float Vertical = pow(Height, 0.58);
float3 Sky = lerp(Horizon, Zenith, Vertical);
float TowardSun = saturate(dot(Ray, normalize(SunDirection)));
float Glow = pow(TowardSun, 9.0) * (0.9 - 0.48 * Vertical) + 0.44 * pow(TowardSun, 84.0);
Sky += SunGlow * Glow;
Sky += SunGlow * (2.3 * smoothstep(0.99975, 0.99994, TowardSun));

if (Ray.z > 0.0)
{
    // Perspective-stretch cloud texture toward the horizon and mix in a finer layer.
    float2 UV = Ray.xy * (0.20 / (Height + 0.21)) + CloudLayout.zw;
    float Body = Texture2DSample(CloudMask, CloudMaskSampler, UV).r;
    float Detail = Texture2DSample(CloudMask, CloudMaskSampler, UV * 2.17 + float2(0.43, 0.27)).r;
    float Field = Body * 0.78 + Detail * 0.22;
    float Amount = smoothstep(CloudLayout.x - 0.05, CloudLayout.x + 0.09, Field)
                   * smoothstep(0.015, 0.14, Height) * CloudLayout.y;
    float Edge = saturate(1.0 - (Field - CloudLayout.x) * 3.1);
    float3 Clouds = lerp(CloudShade, CloudLight, saturate(0.20 + Edge * 0.58 + Detail * 0.18));
    Clouds += SunGlow * (0.40 * Edge * pow(TowardSun, 12.0));
    Sky = lerp(Sky, Clouds, Amount);
}

return Sky;
"""


def prepare():
    path = f"{DIRECTORY}/{NAME}"
    if ASSETS.does_asset_exist(path):
        material = ASSETS.load_asset(path)
        node = EDITOR.get_material_property_input_node(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        if not isinstance(node, unreal.MaterialExpressionCustom):
            raise RuntimeError(f"Expected sky color expression in {path}")
        node.set_editor_property("code", SKY_COLOR)
    else:
        material = TOOLS.create_asset(NAME, DIRECTORY, unreal.Material, unreal.MaterialFactoryNew())
        if not material:
            raise RuntimeError(f"Could not create {path}")
        material.set_editor_property("two_sided", True)
        material.set_editor_property("is_sky", True)
        material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        clouds = unreal.load_asset(CLOUD_MASK)
        if not clouds:
            raise RuntimeError(f"Could not load {CLOUD_MASK}")
        mask = expression(material, unreal.MaterialExpressionTextureObject, -620, 80, texture=clouds,
                          sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if clouds.srgb else
                          unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        layout = expression(material, unreal.MaterialExpressionVectorParameter, -620, 960,
                            parameter_name="CloudLayout", default_value=unreal.LinearColor(.46, .85, .4, .6))
        value = custom(material, SKY_COLOR, unreal.CustomMaterialOutputType.CMOT_FLOAT3, [
            ("ViewDirection", expression(material, unreal.MaterialExpressionCameraVectorWS, -620, 0), ""),
            ("CloudMask", mask, ""),
            ("Zenith", vector(material, "Zenith", (.09, .27, .57), 165), "RGB"),
            ("Horizon", vector(material, "Horizon", (.53, .69, .79), 260), "RGB"),
            ("CloudShade", vector(material, "CloudShade", (.36, .43, .54), 380), "RGB"),
            ("CloudLight", vector(material, "CloudLight", (.87, .93, 1.00), 500), "RGB"),
            ("SunDirection", vector(material, "SunDirection", (-.55, .37, .75), 620), "RGB"),
            ("SunGlow", vector(material, "SunGlow", (.47, .30, .10), 740), "RGB"),
            ("CloudLayout", layout, "RGBA"),
        ])
        connect(value, unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    EDITOR.recompile_material(material)
    if not ASSETS.save_loaded_asset(material):
        raise RuntimeError(f"Could not save {path}")
    unreal.log(f"Prepared battlefield sky material {path}")


if __name__ == "__main__":
    prepare()
