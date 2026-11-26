"""Build the RegimentField level in a dedicated Unreal Editor commandlet."""

import unreal


MAP = "/Game/Battle/Maps/RegimentField"
MATERIALS = "/Game/Battle/Materials"
FAB = "/Game/Fab/Free_LOWPO__Mercenary_Company___Low_Poly_Characters___Weapons"
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
tools = unreal.AssetToolsHelpers.get_asset_tools()


def require_asset(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing asset: {path}")
    return asset


def material(name, color):
    path = f"{MATERIALS}/{name}"
    if assets.does_asset_exist(path):
        return require_asset(path)
    result = tools.create_asset(name, MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
    editor = unreal.MaterialEditingLibrary
    expression = editor.create_material_expression(
        result, unreal.MaterialExpressionConstant3Vector, -270, 0)
    expression.set_editor_property("constant", unreal.LinearColor(*color, 1.0))
    editor.connect_material_property(expression, "", unreal.MaterialProperty.MP_BASE_COLOR)
    roughness = editor.create_material_expression(
        result, unreal.MaterialExpressionConstant, -270, 100)
    roughness.set_editor_property("r", 0.89)
    editor.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    editor.recompile_material(result)
    if not assets.save_loaded_asset(result):
        raise RuntimeError(f"Could not save material: {path}")
    return result


def spawn(actor_class, name, xyz, rotation=(0, 0, 0)):
    pitch, yaw, roll = rotation
    actor = actors.spawn_actor_from_class(
        actor_class, unreal.Vector(*xyz), unreal.Rotator(pitch=pitch, yaw=yaw, roll=roll))
    if not actor:
        raise RuntimeError(f"Could not create {name}")
    actor.set_actor_label(name)
    return actor


def prop(name, mesh, surface, xyz, scale, collides=False):
    actor = spawn(unreal.StaticMeshActor, name, xyz)
    part = actor.get_component_by_class(unreal.StaticMeshComponent)
    part.set_static_mesh(mesh)
    part.set_material(0, surface)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    if not collides:
        part.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    return actor


def main():
    field_class = unreal.load_class(None, "/Script/RegimentBattleRuntime.RegimentBattleField")
    if not field_class:
        raise RuntimeError("Build AgeOfBattlesStasEditor before creating the battle level.")
    if assets.does_asset_exist(MAP):
        raise RuntimeError(f"{MAP} already exists. Move the level to a new name before regenerating.")

    swordsman = require_asset(f"{FAB}/Mercenary_Swordsman")
    archer = require_asset(f"{FAB}/Mercenary_Archer")
    captain = require_asset(f"{FAB}/Mercenary_Captain")
    cube = require_asset("/Engine/BasicShapes/Cube")
    cylinder = require_asset("/Engine/BasicShapes/Cylinder")
    sphere = require_asset("/Engine/BasicShapes/Sphere")
    cone = require_asset("/Engine/BasicShapes/Cone")

    grass = material("M_FieldGrass", (0.19, 0.31, 0.12))
    field_edge = material("M_FieldEdge", (0.10, 0.22, 0.13))
    soil = material("M_DryTrack", (0.39, 0.29, 0.15))
    rock = material("M_FieldStone", (0.29, 0.29, 0.25))
    bark = material("M_FieldWood", (0.26, 0.15, 0.08))
    blue = material("M_CompanyBlue", (0.019, 0.23, 0.71))
    orange = material("M_CompanyOrange", (0.88, 0.14, 0.035))

    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    if not world:
        raise RuntimeError(f"Could not create level: {MAP}")

    prop("Meadow", cube, grass, (0, 0, -26), (118, 82, 0.52), True)
    prop("Dirt track", cube, soil, (0, 0, 0.6), (118, 2.4, 0.012))
    for y in (-3780, 3780):
        prop(f"Meadow border {y}", cube, field_edge, (0, y, 0.4), (118, 6.4, 0.008))

    for index, (x, y, width) in enumerate(
        [(-3390, 2390, 13), (3800, 2180, 17), (-3750, -2150, 12), (4210, -1950, 14)]
    ):
        prop(f"Mound {index + 1}", sphere, field_edge, (x, y, -60), (width, 7, 3.8))
        for tree in range(3):
            tx = x + 210 * (tree - 1)
            ty = y + 140 * ((tree % 2) - 0.5)
            prop(f"Trunk {index}-{tree}", cylinder, bark, (tx, ty, 165), (0.22, 0.22, 2.4))
            prop(f"Crown {index}-{tree}", cone, field_edge, (tx, ty, 346), (1.9, 1.9, 2.8))
        prop(f"Stone {index + 1}", sphere, rock, (x - 340, y - 290, 21), (1.1, 0.65, 0.62))

    for x, team, color in [(-2670, "Blue", blue), (2670, "Orange", orange)]:
        for y in (-1240, 1240):
            prop(f"{team} standard post {y}", cylinder, bark, (x, y, 140), (0.12, 0.12, 2.8))
            prop(f"{team} standard {y}", cube, color, (x, y + 50, 244), (0.08, 1.1, 0.61))

    sun = spawn(unreal.DirectionalLight, "Afternoon light", (0, 0, 1500), (-49, -27, 0))
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(5.0)
    spawn(unreal.SkyAtmosphere, "Sky atmosphere", (0, 0, 0))
    sky = spawn(unreal.SkyLight, "Sky fill", (0, 0, 600))
    sky.get_component_by_class(unreal.SkyLightComponent).set_editor_property(
        "real_time_capture", True)

    camera = spawn(unreal.CameraActor, "Battle overview camera", (0, -4150, 5300), (-52, 90, 0))
    camera.get_component_by_class(unreal.CameraComponent).set_field_of_view(50.0)
    spawn(unreal.PlayerStart, "Player start", (0, -1540, 100))
    field = spawn(field_class, "BattleCore — two companies of 50", (0, 0, 0))
    for name, value in (
        ("overview_camera", camera),
        ("swordsman_mesh", swordsman),
        ("archer_mesh", archer),
        ("captain_mesh", captain),
    ):
        field.set_editor_property(name, value)
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP):
        raise RuntimeError(f"Could not save level: {MAP}")
    unreal.log(f"RegimentField created: {MAP}")


main()
