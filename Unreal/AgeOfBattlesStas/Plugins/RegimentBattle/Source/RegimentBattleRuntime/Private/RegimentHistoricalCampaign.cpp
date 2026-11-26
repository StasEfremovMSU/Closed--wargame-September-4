#include "RegimentHistoricalCampaign.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

THIRD_PARTY_INCLUDES_START
#include "battle/World.h"
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "RegimentHistoricalCampaign"

namespace RegimentHistoricalCampaign
{
namespace
{
constexpr const TCHAR* SaveVersion = TEXT("RegimentCampaignProgress 1");

FStage Stage(const TCHAR* Id, FText Label)
{
    FGuid Guid;
    const bool bParsed = FGuid::ParseExact(Id, EGuidFormats::Digits, Guid);
    check(bParsed);
    return {Guid, MoveTemp(Label)};
}
}

const TArray<FCampaign>& Catalog()
{
    static const TArray<FCampaign> Routes{
        {TEXT("macedon"), LOCTEXT("Alexander", "Александр Македонский"),
            LOCTEXT("AlexanderDescription", "Македония · от войска Филиппа II в Греции до похода к Гидаспу"),
            {
                Stage(TEXT("120A3C1B7539440E8D53A2A671463891"), LOCTEXT("Chaeronea", "Херонея · 338 до н. э.")),
                Stage(TEXT("47EB0F39FBB54ECCA5CC7C9744028A40"), LOCTEXT("Granicus", "Граник · 334 до н. э.")),
                Stage(TEXT("458A83AA2AFE423F8A6DE3E17F556DBC"), LOCTEXT("Issus", "Исс · 333 до н. э.")),
                Stage(TEXT("7A83238CB3734E1896F84565A8C2ACD1"), LOCTEXT("Gaugamela", "Гавгамелы · 331 до н. э.")),
                Stage(TEXT("CF730162A275449991AFABD448FFA2B8"), LOCTEXT("Hydaspes", "Гидасп · 326 до н. э."))
            }},
        {TEXT("france"), LOCTEXT("Napoleon", "Наполеон Бонапарт"),
            LOCTEXT("NapoleonDescription", "Франция · от итальянского похода первого консула до Лейпцига"),
            {
                Stage(TEXT("99B894D5BDAE4DEC91627D84D24FE7C9"), LOCTEXT("Marengo", "Маренго · 1800")),
                Stage(TEXT("478F6CC7240352E4A8BA99FCF4DF769A"), LOCTEXT("Austerlitz", "Аустерлиц · 1805")),
                Stage(TEXT("33BC0C6F226A508195759648C085EA58"), LOCTEXT("Jena", "Йена · 1806")),
                Stage(TEXT("36FE71C75AFD565ABB6E7B4A60281750"), LOCTEXT("Borodino", "Бородино · 1812")),
                Stage(TEXT("6DC62DF89CFA521AB4829690956C5B22"), LOCTEXT("Leipzig", "Лейпциг · 1813"))
            }},
        {TEXT("france"), LOCTEXT("HundredYears", "Столетняя война"),
            LOCTEXT("HundredYearsDescription", "Франция · от первых поражений до победы при Кастийоне"),
            {
                Stage(TEXT("B60CD12CBABE4FF6894C48ECB4F96421"), LOCTEXT("Crecy", "Креси · 1346")),
                Stage(TEXT("56A27E2BAD0C4AAF9FCA2F9869C34DF2"), LOCTEXT("Agincourt", "Азенкур · 1415")),
                Stage(TEXT("3C6B98A848E1574FB7B55AAC7B34E809"), LOCTEXT("Patay", "Пате · 1429")),
                Stage(TEXT("6CA83B6340274A0B92DBBDC6DCDBE4AD"), LOCTEXT("Castillon", "Кастийон · 1453"))
            }}
    };
    return Routes;
}

FString SavePath()
{
    return FPaths::ProjectSavedDir() / TEXT("CampaignProgress/Progress.txt");
}

bool FProgress::IsComplete(int32 Campaign, int32 Stage) const
{
    const auto& Routes = Catalog();
    return Routes.IsValidIndex(Campaign) && Routes[Campaign].Stages.IsValidIndex(Stage) &&
        Victories.Contains(Routes[Campaign].Stages[Stage].MissionId);
}

bool FProgress::IsUnlocked(int32 Campaign, int32 Stage) const
{
    const auto& Routes = Catalog();
    return Routes.IsValidIndex(Campaign) && Routes[Campaign].Stages.IsValidIndex(Stage) &&
        (Stage == 0 || IsComplete(Campaign, Stage - 1));
}

bool FProgress::RecordOutcome(TOptional<FRun> Run, battle::BattleOutcome Outcome, int32 Winner)
{
    if (!Run || Outcome != battle::BattleOutcome::Victory || Winner != 0 ||
        !IsUnlocked(Run->Campaign, Run->Stage) || IsComplete(Run->Campaign, Run->Stage))
        return false;

    Victories.Add(Catalog()[Run->Campaign].Stages[Run->Stage].MissionId);
    return true;
}

void FProgress::Load(const FString& Path)
{
    Victories.Reset();
    TArray<FString> Lines;
    const auto Read = [&Lines](const FString& Filename)
    {
        Lines.Reset();
        return FFileHelper::LoadFileToStringArray(Lines, *Filename) &&
            !Lines.IsEmpty() && Lines[0] == SaveVersion;
    };
    if (!Read(Path) && !Read(Path + TEXT(".bak")))
        return;

    for (const auto& Campaign : Catalog())
        for (const auto& Mission : Campaign.Stages)
        {
            if (!Lines.Contains(Mission.MissionId.ToString(EGuidFormats::Digits)))
                break;
            Victories.Add(Mission.MissionId);
        }
}

bool FProgress::Save(const FString& Path) const
{
    TArray<FString> Lines{SaveVersion};
    for (const auto& Campaign : Catalog())
        for (const auto& Mission : Campaign.Stages)
            if (Victories.Contains(Mission.MissionId))
                Lines.Add(Mission.MissionId.ToString(EGuidFormats::Digits));

    auto& Files = IFileManager::Get();
    Files.MakeDirectory(*FPaths::GetPath(Path), true);
    const FString Temporary = Path + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Backup = Path + TEXT(".bak");
    if (!FFileHelper::SaveStringArrayToFile(Lines, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        Files.Delete(*Temporary);
        return false;
    }

    // The low-level Move cannot replace a file atomically. Keep the previous
    // save as a recovery point while publishing the new file.
    if (Files.FileExists(*Path) &&
        (!Files.Delete(*Backup) || !Files.Move(*Backup, *Path, false, false, false, true)))
    {
        Files.Delete(*Temporary);
        return false;
    }

    if (Files.Move(*Path, *Temporary, false, false, false, true))
        return true;

    Files.Delete(*Temporary);
    return false;
}
}

#undef LOCTEXT_NAMESPACE
