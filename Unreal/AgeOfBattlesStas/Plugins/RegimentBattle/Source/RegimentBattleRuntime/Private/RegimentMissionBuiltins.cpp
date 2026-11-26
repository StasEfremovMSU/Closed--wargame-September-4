#include "RegimentMissionPreset.h"
#include "RegimentEraCatalog.h"

#include "Internationalization/Internationalization.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "RegimentMissionBuiltins"

namespace
{
const FGuid StirlingId(0x0C79AE21, 0xF7BC42D8, 0x91DE32A5, 0x598C19E9);
const FGuid ReserveId(0x3EAF8C19, 0x7A714ABD, 0x8F0107C4, 0x1C947D41);
const FGuid TacticsId(0x2C6CDE24, 0x1F8F47C4, 0x8A20B991, 0x284BDC14);
const FGuid HillHoldId(0xA16D79C4, 0xB2474F66, 0x98D7143E, 0x622FCE91);
const FGuid CannaeBenchmarkId(0x39FA5E9A, 0x273F4DCE, 0xB950627B, 0xFA9D8921);
const FGuid AncientRangeId(0x6C326E44, 0xB6484ABB, 0x893D83EE, 0xDB4F9D6F);

bool IsEraMissionId(FGuid Id)
{
    return Id.IsValid() && Id != StirlingId && Id != ReserveId && Id != TacticsId && Id != HillHoldId &&
        Id != MissionSmokeExerciseId && Id != AncientRangeId;
}

void EmptyRoster(FMissionPreset& Preset)
{
    for (auto& Entry : Preset.Roster)
        Entry.bEnabled = false;
}

void Place(FMissionPreset& Preset, int32 Slot, FMissionRosterEntry Entry, float X, float Y)
{
    Entry.Group = 2;
    Entry.StartX = X;
    Entry.StartY = Y;
    Preset.Roster[Slot] = Entry;
}

FMissionPreset StirlingPreset()
{
    FMissionPreset Preset;
    Preset.Name = LOCTEXT("StirlingTitle", "Стерлингский мост, 1297").ToString();
    Preset.Terrain = battle::MarchTerrain::StirlingBridge;
    Preset.Objective = { EMissionGoal::Crossing, 120, 1, 1, 1 };
    Preset.Briefing = LOCTEXT("StirlingBriefing",
        "11 сентября 1297 года шотландцы атаковали английские силы у узкого моста через Форт. "
        "Синие — Шотландия, оранжевые — Англия. За 120 секунд Англии нужно провести один полк через мост или разгромить оборону. Шотландия удерживает выход. "
        "Игровой участок занимает 120 × 76 м. Река спрямлена, ширина моста, численность, позиции и время заданы для упражнения. "
        "Источники — реестр полей сражений Historic Environment Scotland (BTL28) и запись об остатках моста (SM8264).").ToString();
    Preset.Sources = {
        TEXT("https://portal.historicenvironment.scot/apex/f?p=1505:300:::::VIEWTYPE,VIEWREF:designation,BTL28"),
        TEXT("https://portal.historicenvironment.scot/apex/f?p=1505:300:::::VIEWTYPE,VIEWREF:designation,SM8264")
    };
    EmptyRoster(Preset);
    Place(Preset, 0, {0, 0, 28}, -18.f, 0.f);
    Place(Preset, 1, {7, 0, 18}, -24.f, -10.f);
    Place(Preset, 2, {5, 0, 15}, -24.f, 10.f);
    FMissionRosterEntry Vanguard{0, 0};
    Vanguard.bCavalry = true;
    Vanguard.Mounted = 18;
    Place(Preset, MissionRegimentsPerTeam, Vanguard, 12.f, 0.f);
    Place(Preset, MissionRegimentsPerTeam + 1, {20, 2, 8}, 24.f, 0.f);
    Place(Preset, MissionRegimentsPerTeam + 2, {18, 0, 8}, 36.f, 0.f);
    return Preset;
}

FMissionPreset ReservePreset()
{
    FMissionPreset Preset;
    Preset.Name = LOCTEXT("ReserveTitle", "Учение · когда вводить резерв").ToString();
    Preset.Terrain = battle::MarchTerrain::OpenField;
    Preset.Objective = { EMissionGoal::ReserveExercise, 60, 1, 1, 0 };
    Preset.Briefing = LOCTEXT("ReserveBriefing",
        "Под твоим командованием два синих полка. Передовой идёт в бой, второй ждёт приказа. "
        "Нажми «Ввести резерв», когда посчитаешь нужным. Цель — довести резерв до ближнего боя, "
        "пока у передового полка осталось не меньше половины состава. Нажатие отдаёт приказ с задержкой доставки.").ToString();
    EmptyRoster(Preset);
    Place(Preset, 0, {14, 4, 8}, -11.f, 0.f);
    Place(Preset, 1, {12, 0, 4}, -22.f, 9.f);
    Place(Preset, MissionRegimentsPerTeam, {33, 0, 8}, 12.f, 0.f);
    return Preset;
}

FMissionPreset HillHoldPreset()
{
    FMissionPreset Preset;
    if (const auto* Era = RegimentEraCatalog::Find(TEXT("ancient")))
        RegimentMissionPresets::ApplyEra(Preset, *Era);
    Preset.Name = LOCTEXT("HillHoldTitle", "Учение · удержать высоту").ToString();
    Preset.Terrain = battle::MarchTerrain::Hill;
    Preset.bDirectDeployment = true;
    Preset.Objective.Goal = EMissionGoal::HillHold;
    Preset.Objective.TimeLimitSeconds = 90;
    Preset.Briefing = FText::Format(LOCTEXT("HillHoldBriefing",
        "За 90 секунд займите вершину и удерживайте её {0} секунд подряд. "
        "На вершине должна оставаться хотя бы половина исходного состава одного синего полка. "
        "Оранжевые бойцы в строю прерывают отсчёт. "
        "Возьмите управление офицером, отдайте приказ «За мной», а на вершине прикажите полку «Стоять»."),
        MissionHillHoldSeconds).ToString();
    EmptyRoster(Preset);
    Place(Preset, 0, {20, 0, 6}, -20.f, 0.f);
    Place(Preset, 1, {14, 0, 4}, -26.f, -18.f);
    Place(Preset, MissionRegimentsPerTeam, {14, 0, 3}, 27.f, 0.f);
    Place(Preset, MissionRegimentsPerTeam + 1, {10, 0, 3}, 34.f, -16.f);
    return Preset;
}

FMissionPreset TacticsPreset()
{
    FMissionPreset Preset;
    Preset.Name = LOCTEXT("TacticsTitle", "Полигон · полковая тактика").ToString();
    Preset.Terrain = battle::MarchTerrain::OpenField;
    Preset.bDirectDeployment = true;
    Preset.Briefing = LOCTEXT("TacticsBriefing",
        "У каждой стороны два пехотных полка. У оранжевых есть отдельный конный отряд. "
        "Открой «Решения офицеров», чтобы следить за боевым духом, строем и направлением ударов. "
        "В копии миссии можно разрешить стрельбу по полкам в рукопашной и сравнить результаты.").ToString();
    EmptyRoster(Preset);
    Place(Preset, 0, {21, 0, 15}, -13.f, 0.f);
    Place(Preset, 1, {8, 15, 5}, -23.f, 3.f);
    FMissionRosterEntry Cavalry{0, 0};
    Cavalry.bCavalry = true;
    Cavalry.Mounted = 20;
    const int32 Orange = MissionRegimentsPerTeam;
    Place(Preset, Orange, {21, 0, 12}, 13.f, 0.f);
    Place(Preset, Orange + 1, {7, 14, 7}, 23.f, -3.f);
    Place(Preset, Orange + 2, Cavalry, 15.f, 16.f);
    Preset.Roster[Orange + 2].Group = 3;
    return Preset;
}

FMissionPreset SmokeExercisePreset()
{
    FMissionPreset Preset;
    Preset.Name = LOCTEXT("SmokeExerciseTitle", "Полигон · пороховой дым").ToString();
    Preset.Terrain = battle::MarchTerrain::OpenField;
    Preset.bDirectDeployment = true;
    Preset.bSmokeExercise = true;
    Preset.Briefing = LOCTEXT("SmokeExerciseBriefing",
        "Синий полк пороховых стрелков открывает огонь, когда противник сближается. "
        "После выстрелов облака дыма закрывают обзор и рассеиваются. "
        "У оранжевых есть конница на фланге. В замедлении легче увидеть дым и услышать приближение.").ToString();
    EmptyRoster(Preset);
    Place(Preset, 0, {0, 16}, -14.f, -4.f);
    Place(Preset, 1, {4, 0, 13}, -17.f, 13.f);
    Place(Preset, MissionRegimentsPerTeam, {20, 0, 8}, 14.f, -4.f);
    FMissionRosterEntry Cavalry{0, 0};
    Cavalry.bCavalry = true;
    Cavalry.Mounted = 14;
    Place(Preset, MissionRegimentsPerTeam + 1, Cavalry, 26.f, 13.f);
    return Preset;
}

FMissionPreset AncientRangePreset(const FRegimentEraDefinition& Era)
{
    FMissionPreset Preset;
    RegimentMissionPresets::ApplyEra(Preset, Era);
    Preset.Era.BlueFaction = TEXT("persia");
    Preset.Era.OrangeFaction = TEXT("carthage");
    Preset.Name = LOCTEXT("AncientRangeTitle", "Полигон · катапульты, баллисты и колесницы").ToString();
    Preset.Terrain = battle::MarchTerrain::OpenField;
    Preset.bDirectDeployment = true;
    Preset.Briefing = LOCTEXT("AncientRangeBriefing",
        "Учебный бой Персии и Карфагена. Слева у персов стоят две катапульты, справа две баллисты. "
        "Карфаген выставил против катапульт африканских слонов, а против баллист пехоту. "
        "Персидские индийские слоны и серпоносные колесницы поддерживают свои полки. "
        "Катапульта стреляет по дуге на 10–40 м. Баллиста остаётся на месте и стреляет по прямой до 30 м. "
        "Экипаж слона стреляет из лука. Для удара колесницам нужен разгон.").ToString();

    EmptyRoster(Preset);
    const auto PlacePlatform = [&Preset](int32 Slot, battle::BattlePlatform Type, int32 Count, float X, float Y, int32 Group)
    {
        FMissionRosterEntry Entry;
        Entry.Platform = Type;
        Entry.PlatformCount = Count;
        Place(Preset, Slot, Entry.Clamped(), X, Y);
        Preset.Roster[Slot].Group = Group;
    };
    using enum battle::BattlePlatform;
    Place(Preset, 0, {18, 5, 10}, -11.f, 0.f);
    PlacePlatform(1, IndianElephant, 2, -18.f, -9.f, 2);
    PlacePlatform(2, ScythedChariot, 3, -15.f, 11.f, 3);
    PlacePlatform(3, Catapult, 2, -20.f, -23.f, 1);
    PlacePlatform(4, Ballista, 2, -12.f, 23.f, 3);
    const int32 Orange = MissionRegimentsPerTeam;
    Place(Preset, Orange, {22, 6, 10}, 12.f, 0.f);
    PlacePlatform(Orange + 1, AfricanElephant, 2, 12.f, -21.f, 1);
    Place(Preset, Orange + 2, {16, 0, 8}, 9.f, 23.f);
    Preset.Roster[Orange + 2].Group = 3;
    return Preset;
}

struct FHistoricalPreset
{
    FMissionPresetListItem Item;
    FMissionPreset Preset;
};

TConstArrayView<FHistoricalPreset> HistoricalPresets()
{
    static const auto Presets = []
    {
        TArray<FHistoricalPreset> Loaded;
        const FString Directory = FPaths::ProjectContentDir() / TEXT("Battle/HistoricalPresets");
        for (auto Item : RegimentMissionPresets::List(Directory))
        {
            if (!IsEraMissionId(Item.Id) || RegimentEraCatalog::All().ContainsByPredicate(
                [&Item](const auto& Era) { return Era.MissionId == Item.Id; }))
                continue;

            FMissionPreset Preset;
            FText Error;
            if (!RegimentMissionPresets::Load(Directory, Item.Id, Preset, Error))
            {
                UE_LOG(LogTemp, Warning, TEXT("Could not load built-in historical mission %s: %s"),
                    *Item.Id.ToString(), *Error.ToString());
                continue;
            }
            Preset.EditableCopyId.Invalidate();
            Item.bBuiltIn = true;
            Item.bHistorical = Item.Id != CannaeBenchmarkId && Preset.Era.Id != TEXT("fantasy");
            Item.EraId = Preset.Era.Id;
            if (Item.bHistorical)
                Preset.HistoricalIdentity = Item.Id;
            Loaded.Add({MoveTemp(Item), MoveTemp(Preset)});
        }
        return Loaded;
    }();
    return Presets;
}

FMissionPreset EraPreset(const FRegimentEraDefinition& Era)
{
    FMissionPreset Preset;
    RegimentMissionPresets::ApplyEra(Preset, Era);
    Preset.Name = Era.MissionName;
    Preset.Briefing = Era.Briefing;
    Preset.Roster = Era.Roster;
    Preset.Terrain = Era.Terrain;
    Preset.bDirectDeployment = true;
    return Preset;
}
}

TArray<FMissionPresetListItem> RegimentMissionPresets::BuiltIns()
{
    TArray<FMissionPresetListItem> Presets = {
        {FGuid(), LOCTEXT("DefaultTitle", "Тестовая миссия").ToString(), true},
        {StirlingId, LOCTEXT("StirlingTitle", "Стерлингский мост, 1297").ToString(), true, {}, false, true},
        {ReserveId, LOCTEXT("ReserveTitle", "Учение · когда вводить резерв").ToString(), true, {}, true},
        {HillHoldId, LOCTEXT("HillHoldTitle", "Учение · удержать высоту").ToString(), true, {}, true},
        {TacticsId, LOCTEXT("TacticsTitle", "Полигон · полковая тактика").ToString(), true, {}, true},
        {MissionSmokeExerciseId, LOCTEXT("SmokeExerciseTitle", "Полигон · пороховой дым").ToString(), true}
    };
    for (const FRegimentEraDefinition& Era : RegimentEraCatalog::All())
        if (IsEraMissionId(Era.MissionId))
            Presets.Add({Era.MissionId, Era.MissionName, true, Era.Id});
    if (RegimentEraCatalog::Find(TEXT("ancient")))
        Presets.Add({AncientRangeId, LOCTEXT("AncientRangeTitle", "Полигон · катапульты, баллисты и колесницы").ToString(),
            true, TEXT("ancient"), false, false, true});
    for (const auto& Historical : HistoricalPresets())
        Presets.Add(Historical.Item);
    return Presets;
}

bool RegimentMissionPresets::LoadBuiltIn(FGuid Id, FMissionPreset& OutPreset)
{
    if (Id == StirlingId)
    {
        OutPreset = StirlingPreset();
        OutPreset.HistoricalIdentity = Id;
    }
    else if (Id == ReserveId)
        OutPreset = ReservePreset();
    else if (Id == TacticsId)
        OutPreset = TacticsPreset();
    else if (Id == HillHoldId)
        OutPreset = HillHoldPreset();
    else if (Id == MissionSmokeExerciseId)
        OutPreset = SmokeExercisePreset();
    else if (Id == AncientRangeId)
    {
        const auto* Era = RegimentEraCatalog::Find(TEXT("ancient"));
        if (!Era)
            return false;
        OutPreset = AncientRangePreset(*Era);
    }
    else if (!Id.IsValid())
    {
        OutPreset = FMissionPreset{};
        OutPreset.Name = LOCTEXT("DefaultTitle", "Тестовая миссия").ToString();
    }
    else
    {
        for (const FRegimentEraDefinition& Era : RegimentEraCatalog::All())
        {
            if (Id != Era.MissionId)
                continue;
            OutPreset = EraPreset(Era);
            return true;
        }
        for (const auto& Historical : HistoricalPresets())
        {
            if (Id == Historical.Item.Id)
            {
                OutPreset = Historical.Preset;
                return true;
            }
        }
        return false;
    }
    return true;
}

#undef LOCTEXT_NAMESPACE
