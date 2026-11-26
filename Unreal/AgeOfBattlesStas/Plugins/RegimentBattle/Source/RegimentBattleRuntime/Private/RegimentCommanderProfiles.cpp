#include "RegimentCommanderProfiles.h"

#include "Engine/Texture2D.h"
#include "Internationalization/Internationalization.h"
#include "Misc/Crc.h"
#include "RegimentMissionPreset.h"
#include "UObject/StrongObjectPtr.h"

THIRD_PARTY_INCLUDES_START
#include "battle/World.h"
#include "battle/command/Services.h"
THIRD_PARTY_INCLUDES_END

namespace RegimentCommanderProfiles
{
namespace
{
using battle::command::Level;

struct FNamePool
{
    const TCHAR* Factions;
    const TCHAR* Given;
    const TCHAR* Family;
};

const FNamePool NamePools[] = {
    {TEXT("egypt"), TEXT("Хори|Панехси|Нахт|Иуф|Пасер"), TEXT("")},
    {TEXT("greece|sparta"), TEXT("Дамон|Клеарх|Мнесифей|Архидем|Дориэй"), TEXT("")},
    {TEXT("macedon|diadochi"), TEXT("Аминта|Кратер|Полиперхонт|Антиген|Леоннат"), TEXT("")},
    {TEXT("persia|parthians"), TEXT("Артавазд|Артабаз|Оксатр|Фарнабаз|Митридат"), TEXT("")},
    {TEXT("porus"), TEXT("Виджая|Рудра|Нанда|Арджуна|Шивадатта"), TEXT("")},
    {TEXT("carthage"), TEXT("Магон|Ганнон|Гамилькар|Гискон|Бомилькар"), TEXT("")},
    {TEXT("rome"), TEXT("Марк|Луций|Гай|Квинт|Секст"), TEXT("Валерий|Клавдий|Фульвий|Фабий|Кальпурний")},
    {TEXT("gauls"), TEXT("Эпоредориг|Кот Худ|Седулл|Виридомар|Биториг"), TEXT("")},
    {TEXT("scythians|sarmatians"), TEXT("Иданфирс|Савмак|Таксак|Скилак|Ариант"), TEXT("")},
    {TEXT("rus"), TEXT("Мстислав|Всеволод|Борис|Дмитрий|Ярослав"), TEXT("Фёдорович|Игоревич|Петрович|Романович|Якунович")},
    {TEXT("mongols|golden_horde"), TEXT("Бора|Буку|Хутуг|Елюй|Кадан"), TEXT("")},
    {TEXT("teutonic_order|livonian_order|otto_coalition|german_principalities|austria|saxony"),
        TEXT("Генрих|Конрад|Вернер|Дитрих|Фридрих"), TEXT("фон Берг|фон Марбург|фон Вальдек|фон Линден|фон Штайн")},
    {TEXT("crusaders|normans|capetian_france|france|burgundy"),
        TEXT("Гийом|Робер|Эд|Рауль|Анри"), TEXT("де Бриенн|де Монфор|де Куси|де Пуазье|де Бомон")},
    {TEXT("saracens"), TEXT("Юсуф|Халид|Омар|Ибрахим|Саид"), TEXT("ибн Зайд|ибн Халиль|ибн Махмуд|ибн Абдаллах|ибн Фарис")},
    {TEXT("byzantium"), TEXT("Иоанн|Никифор|Феодор|Андроник|Михаил"), TEXT("Дука|Мелиссин|Палеолог|Аргир|Ласкарь")},
    {TEXT("poland|commonwealth|poland_lithuania|polish_lithuanian_commonwealth"),
        TEXT("Ян|Миколай|Павел|Анджей|Станислав"), TEXT("Заремба|Домарадзкий|Лис|Мазур|Соколовский")},
    {TEXT("lithuania"), TEXT("Вайдотас|Скиргайла|Довмонт|Товтивил|Виштартас"), TEXT("")},
    {TEXT("anglo_saxons"), TEXT("Эдвин|Моркэр|Гирт|Тости|Эльфгар"), TEXT("")},
    {TEXT("england|lancaster|york|britain"), TEXT("Джон|Эдмунд|Томас|Уильям|Ричард"),
        TEXT("Грей|Холл|Уорд|Эшли|Фостер")},
    {TEXT("swiss"), TEXT("Ульрих|Ганс|Арнольд|Каспар|Клаус"), TEXT("Кунц|Бергер|Шталь|Брунер|Келлер")},
    {TEXT("hussites"), TEXT("Ян|Богуслав|Иржи|Петр|Ондржей"), TEXT("Главач|Соколь|Куба|Гаек|Шимек")},
    {TEXT("italian_states"), TEXT("Франческо|Лоренцо|Джованни|Паоло|Антонио"),
        TEXT("Орсини|Росси|Колонна|Фарнезе|Бентивольо")},
    {TEXT("ottomans"), TEXT("Мехмед|Али|Мустафа|Синан|Якуб"), TEXT("бей|ага")},
    {TEXT("russian_tsardom|russia"), TEXT("Иван|Пётр|Алексей|Фёдор|Василий"),
        TEXT("Бутурлин|Репнин|Титов|Головин|Волков")},
    {TEXT("cossacks"), TEXT("Остап|Микита|Иван|Данило|Тарас"), TEXT("Тур|Сокол|Бондарь|Коваль|Ткач")},
    {TEXT("sweden"), TEXT("Карл|Эрик|Аксель|Густав|Юхан"), TEXT("Флеминг|Банер|Хорн|Столь|Линд")},
    {TEXT("spain"), TEXT("Хуан|Диего|Луис|Алонсо|Фернандо"), TEXT("де Рохас|де Леон|де Мендоса|де Лара|де Толедо")},
    {TEXT("portugal"), TEXT("Жуан|Дуарте|Антониу|Педру|Мануэл"), TEXT("Пинту|Соареш|Перейра|Мендеш|Рибейру")},
    {TEXT("denmark_norway"), TEXT("Нильс|Сёрен|Кристиан|Оле|Фредерик"), TEXT("Браге|Якобсен|Ланге|Юль|Лунд")},
    {TEXT("prussia"), TEXT("Август|Фридрих|Эрнст|Людвиг|Карл"),
        TEXT("фон Гольц|фон Клейст|фон Штольберг|фон Риттер|фон Ведель")},
    {TEXT("tokugawa|takeda|oda|uesugi|shimazu|hojo|mori|imagawa"),
        TEXT("Масанори|Таданобу|Нобумаса|Харунобу|Ёсимаса"), TEXT("Мацуда|Окубо|Сугияма|Окабэ|Сакаи")},
    {TEXT("humans"), TEXT("Альбрехт|Мартин|Эдвин|Отто|Гуго"), TEXT("Вереск|Кремень|Осока|Ворон|Дорн")},
    {TEXT("elves"), TEXT("Элериан|Идрис|Сейлар|Талиэн|Наэр"), TEXT("Лунный Лист|Серебряный Вяз|Ясный Ручей")},
    {TEXT("orcs|goblins|beastmen"), TEXT("Граз|Оргак|Скуг|Торг|Урук"), TEXT("Кривой Клык|Рваное Ухо|Чёрная Шкура")},
    {TEXT("undead|demons"), TEXT("Моракс|Нерет|Ирран|Саргон|Варс"), TEXT("Безмолвный|Сумрачный|Холодный")},
    {TEXT("dwarves|giants"), TEXT("Брокк|Хальд|Дагр|Торв|Йорн"), TEXT("Каменная Рука|Горный|Крепкий Молот")},
    {TEXT("magical_creatures"), TEXT("Авран|Иллир|Солар|Навар|Оран"), TEXT("Искра|Пепел|Тихий Ветер")},
};

const FNamePool* FindPool(const FString& Era, const FString& Faction)
{
    static const FNamePool FrenchNapoleonic{TEXT("france"), TEXT("Жан|Луи|Пьер|Жозеф|Шарль"),
        TEXT("Моро|Дюваль|Лебрен|Мартен|Ренье")};
    if (Era == TEXT("napoleonic") && Faction == TEXT("france"))
        return &FrenchNapoleonic;
    const FString Token = TEXT("|") + Faction + TEXT("|");
    for (const auto& Pool : NamePools)
        if ((FString(TEXT("|")) + Pool.Factions + TEXT("|")).Contains(Token, ESearchCase::CaseSensitive))
            return &Pool;
    return nullptr;
}

uint64 Mix(uint64 Value)
{
    Value = (Value ^ (Value >> 30)) * 0xbf58476d1ce4e5b9ull;
    Value = (Value ^ (Value >> 27)) * 0x94d049bb133111ebull;
    return Value ^ (Value >> 31);
}

uint64 ProfileSeed(const FMissionPreset& Preset, const FString& Faction, const battle::command::CommandNode& Node)
{
    uint64 Hash = 0xcbf29ce484222325ull;
    for (TCHAR Character : Preset.Era.Id + TEXT("/") + Faction)
        Hash = (Hash ^ uint64(Character)) * 0x100000001b3ull;
    return Mix(Hash ^ Node.commander.value ^ (uint64(Node.team) << 32) ^ (uint64(Node.level) << 48));
}

FString NamePart(const TCHAR* Text, uint64 Seed)
{
    TArray<FString> Parts;
    FString(Text).ParseIntoArray(Parts, TEXT("|"));
    return Parts.IsEmpty() ? FString() : Parts[int32(Seed % uint64(Parts.Num()))];
}

FString SeededName(const FNamePool* Pool, uint64 Seed)
{
    if (!Pool)
        Pool = FindPool(TEXT("high_medieval"), TEXT("rus"));
    const FString Given = NamePart(Pool->Given, Seed);
    const FString Family = NamePart(Pool->Family, Mix(Seed));
    return Family.IsEmpty() ? Given : Given + TEXT(" ") + Family;
}

enum class EHeadwear : uint8 { Hair, Diadem, Helmet, Turban, Crown, Bicorne, Kabuto, Shako, Tricorne };
struct FHistoricalCommander
{
    const TCHAR* Name;
    EHeadwear Hat;
    uint32 Background;
    uint32 Uniform;
    uint32 Hair;
    uint32 Skin;
    bool bBeard;
    uint8 Personal;
    uint8 General;
    uint8 Rank;
};

// Historical roles use the Alexander Virtual Museum, Musée de l'Armée, and National Army Museum.
// https://alexanderthegreatmuseum.gr/doc/en/
// https://www.musee-armee.fr/au-programme/expositions/detail/napoleon-stratege.html
// https://www.musee-armee.fr/ExpoChevaliersBombardes/visitor-itinerary.html
// https://www.nam.ac.uk/explore/battle-waterloo
// https://museum-artillery.ru/en/main-exposition/mikhail-kutuzov-and-the-war-of-1812.html
// https://penelope.uchicago.edu/Thayer/E/Roman/Texts/Polybius/15%2A.html
// https://www.gutenberg.org/files/46976/46976-h/46976-h.htm
// https://www.napoleon.org/magazine/lieux/marengo
// https://www.musee-armee.fr/fileadmin/user_upload/Documents/Support-panneaux-jeune/6-langage-heraldique-chevaliers-bombardes.pdf
// https://www.historicenvironment.scot/visit/all/stirling-old-bridge/history-and-stories/
// https://www.english-heritage.org.uk/visit/places/1066-battle-of-hastings-abbey-and-battlefield/history-and-stories/what-happened-battle-hastings/
// https://www.english-heritage.org.uk/learn/teaching-resources/teaching-history/teaching-medieval/
// https://battle-poltava.org.ua/en/exposition/
// https://col-militarymaps-frontend.rct.uk/other-18th/19th-century-conflicts/plan-of-ivangorod-and-narva-1700-ivangorod-leningrad-russia-59deg2218n-28deg1259e-narva-ida-virumaa-0
// Accessed 2026-11-26. Portraits and numeric traits are game interpretations.
enum ECommander : uint8
{
    Alexander, Philip, Darius, Porus, Napoleon, Kutuzov, Wellington, PhilipVI, EdwardIII,
    CharlesAlbret, HenryV, LaHire, Talbot, Bureau, Hannibal, Scipio, Varro, CharlesXII, PeterI,
    Joan, Melas, Wallace, Warenne, WilliamNormandy, Harold, HenryTudor, RichardIII, Croy, NoCommander
};
const FHistoricalCommander Historical[] = {
    {TEXT("Александр Македонский"), EHeadwear::Diadem, 0x424B69, 0xA1493C, 0x654331, 0xDEAD82, false, 9, 8, 2},
    {TEXT("Филипп II Македонский"), EHeadwear::Helmet, 0x383E54, 0x684045, 0x3E3334, 0xC38C68, true, 3, 7, 6},
    {TEXT("Дарий III"), EHeadwear::Crown, 0x4C395A, 0x543B76, 0x231F25, 0xB77755, true, 2, 4, 8},
    {TEXT("Пор"), EHeadwear::Turban, 0x584037, 0x46765C, 0x252127, 0xAB7051, true, 6, 6, 9},
    {TEXT("Наполеон Бонапарт"), EHeadwear::Bicorne, 0x33495C, 0x2D425D, 0x3D362F, 0xDFBA92, false, 5, 9, 1},
    {TEXT("Михаил Кутузов"), EHeadwear::Hair, 0x374842, 0x364B38, 0xC8BBAB, 0xDBAC8D, false, 1, 8, 9},
    {TEXT("Артур Уэлсли"), EHeadwear::Bicorne, 0x5C413C, 0x8F383E, 0x5C4638, 0xDDB595, false, 4, 7, 7},
    {TEXT("Филипп VI Валуа"), EHeadwear::Crown, 0x324166, 0x40598A, 0x64402D, 0xD6A37F, true, 5, 4, 7},
    {TEXT("Эдуард III"), EHeadwear::Crown, 0x633A3E, 0x7C333D, 0x8E6640, 0xE9BC96, true, 6, 8, 7},
    {TEXT("Шарль д’Альбре"), EHeadwear::Helmet, 0x424864, 0x54698F, 0x3F2D29, 0xD6AB88, true, 8, 4, 6},
    {TEXT("Генрих V"), EHeadwear::Crown, 0x574245, 0x784049, 0x524133, 0xE2AB82, false, 5, 7, 9},
    {TEXT("Этьен де Виньоль"), EHeadwear::Helmet, 0x534554, 0x70455E, 0x582F2B, 0xC58B68, true, 9, 5, 1},
    {TEXT("Джон Толбот"), EHeadwear::Helmet, 0x4D3C39, 0x835037, 0xA2978A, 0xCD9D84, true, 3, 6, 3},
    {TEXT("Жан Бюро"), EHeadwear::Hair, 0x394449, 0x526266, 0x4B3934, 0xD6A281, false, 2, 7, 8},
    {TEXT("Ганнибал Барка"), EHeadwear::Helmet, 0x66493C, 0x905E3E, 0x322827, 0xAF795A, true, 8, 9, 4},
    {TEXT("Сципион Африканский"), EHeadwear::Diadem, 0x5C393E, 0x863743, 0x5F4538, 0xCFA17D, false, 5, 8, 5},
    {TEXT("Гай Теренций Варрон"), EHeadwear::Helmet, 0x53363D, 0x7F3541, 0x684636, 0xC49070, false, 8, 3, 2},
    {TEXT("Карл XII"), EHeadwear::Bicorne, 0x2F4C65, 0x314D82, 0x775B43, 0xD9AA8C, false, 9, 6, 2},
    {TEXT("Пётр I"), EHeadwear::Hair, 0x4A5041, 0x424B3B, 0x46372E, 0xCF997B, true, 7, 7, 5},
    {TEXT("Жанна д’Арк"), EHeadwear::Hair, 0x414A69, 0x6E7B98, 0x432E2A, 0xE2AB89, false, 8, 9, 4},
    {TEXT("Михаэль фон Мелас"), EHeadwear::Bicorne, 0x494344, 0xA99B83, 0x988B7E, 0xD6AD91, false, 3, 6, 6},
    {TEXT("Уильям Уоллес"), EHeadwear::Hair, 0x3D5354, 0x365B55, 0x72523A, 0xD3A580, true, 8, 8, 1},
    {TEXT("Джон де Варенн"), EHeadwear::Helmet, 0x514642, 0x746141, 0x9B825E, 0xD3A07C, true, 4, 6, 6},
    {TEXT("Вильгельм Нормандский"), EHeadwear::Helmet, 0x4B414A, 0x6D4657, 0x8D6241, 0xDCAD85, false, 6, 8, 4},
    {TEXT("Гарольд II Годвинсон"), EHeadwear::Crown, 0x4F4241, 0x733F3D, 0x58423A, 0xD5A18A, true, 3, 6, 9},
    {TEXT("Генрих Тюдор"), EHeadwear::Crown, 0x3E4951, 0x476155, 0x704F33, 0xDEAF91, false, 4, 7, 8},
    {TEXT("Ричард III"), EHeadwear::Crown, 0x474456, 0x4A3D53, 0x382C29, 0xCAA087, false, 9, 4, 3},
    {TEXT("Шарль Эжен де Круа"), EHeadwear::Hair, 0x55443F, 0x705242, 0xAAA093, 0xCEA483, false, 2, 7, 8},
};
static_assert(UE_ARRAY_COUNT(Historical) == NoCommander);

struct FHistoricalBattle
{
    FGuid Mission;
    ECommander Blue;
    ECommander Orange;
    ECommander BlueGroup = NoCommander;
};

const FHistoricalBattle HistoricalBattles[] = {
    {FGuid(0x120A3C1B, 0x7539440E, 0x8D53A2A6, 0x71463891), Philip, NoCommander, Alexander},
    {FGuid(0x47EB0F39, 0xFBB54ECC, 0xA5CC7C97, 0x44028A40), Alexander, NoCommander},
    {FGuid(0x458A83AA, 0x2AFE423F, 0x8A6DE3E1, 0x7F556DBC), Alexander, Darius},
    {FGuid(0x7A83238C, 0xB3734E18, 0x96F84565, 0xA8C2ACD1), Alexander, Darius},
    {FGuid(0xCF730162, 0xA2754499, 0x91AFABD4, 0x48FFA2B8), Alexander, Porus},
    {FGuid(0x99B894D5, 0xBDAE4DEC, 0x91627D84, 0xD24FE7C9), Napoleon, Melas},
    {FGuid(0x478F6CC7, 0x240352E4, 0xA8BA99FC, 0xF4DF769A), Napoleon, Kutuzov},
    {FGuid(0x33BC0C6F, 0x226A5081, 0x95759648, 0xC085EA58), Napoleon, NoCommander},
    {FGuid(0x36FE71C7, 0x5AFD565A, 0xBB6E7B4A, 0x60281750), Napoleon, Kutuzov},
    {FGuid(0x6DC62DF8, 0x9CFA521A, 0xB4829690, 0x956C5B22), Napoleon, NoCommander},
    {FGuid(0x7E801C0A, 0x1E9941BD, 0x983E0DFE, 0x4DC07B33), Napoleon, Wellington},
    {FGuid(0xB60CD12C, 0xBABE4FF6, 0x894C48EC, 0xB4F96421), PhilipVI, EdwardIII},
    {FGuid(0x594814A7, 0x04264561, 0xBF9D5E8C, 0xF0E04E9), HenryV, CharlesAlbret},
    {FGuid(0x56A27E2B, 0xAD0C4AAF, 0x9FCA2F98, 0x69C34DF2), CharlesAlbret, HenryV},
    {FGuid(0x3C6B98A8, 0x48E1574F, 0xB55AAC7B, 0x34E809), LaHire, Talbot, Joan},
    {FGuid(0x6CA83B63, 0x40274A0B, 0x92DBBDC6, 0xDBE4AD), Bureau, Talbot},
    {FGuid(0xBD8E7423, 0xCDA541E0, 0xAD6DAF78, 0xD4C9F547), Varro, Hannibal},
    {FGuid(0x03BAE43F, 0x906A4969, 0xAD2D5FE8, 0x5C976925), Scipio, Hannibal},
    {FGuid(0x0C79AE21, 0xF7BC42D8, 0x91DE32A5, 0x598C19E9), Wallace, Warenne},
    {FGuid(0xE9487555, 0x480B53DD, 0xB55F05CE, 0xE1885F45), WilliamNormandy, Harold},
    {FGuid(0x22E674A1, 0x6632562B, 0x9E4F1718, 0xE52E2859), HenryTudor, RichardIII},
    {FGuid(0x655310A0, 0x48854A4A, 0xBDE3DE48, 0x9EE8DA84), CharlesXII, PeterI},
    {FGuid(0x845BCC52, 0xB6655AA6, 0xA8E31464, 0x0A1E99B2), CharlesXII, Croy},
};

const FHistoricalCommander* ByName(const FString& Name)
{
    for (const auto& Commander : Historical)
        if (Name.Equals(Commander.Name, ESearchCase::CaseSensitive))
            return &Commander;
    return nullptr;
}

bool IsMountedGroup(const battle::Scenario& Scenario, const battle::command::CommandNode& Node)
{
    return std::ranges::any_of(Scenario.units, [&](const auto& Unit)
    {
        return Unit.regiment && Unit.troop == battle::UnitTroop::Mounted &&
            std::ranges::find(Node.children, battle::command::NodeId{Unit.regiment.value}) != Node.children.end();
    });
}

constexpr battle::Tick QuickCommitmentMaximum = 7;

battle::Tick QuickArmyInterval(bool bAdaptive)
{
    return bAdaptive ? 104 : 40;
}

void ApplyTraits(battle::UnitSpec& Unit, battle::OfficerSpec& Officer, battle::command::OfficerProfile& Profile,
    Level Rank, uint8 Personal, uint8 General, uint8 RankRating, bool bAdaptive)
{
    const float P = (Personal % 5) * .2f;
    const float G = (General % 5) * .2f;
    const float R = (RankRating % 5) * .2f;
    if (Personal >= 5)
    {
        Unit.speed *= 1.08f + .12f * P;
        Unit.health *= .95f - .12f * P;
    }
    else
    {
        Unit.armour += .10f + .25f * P;
        Unit.health *= 1.12f + .15f * P;
        Unit.speed *= .96f - .10f * P;
    }

    if (General >= 5)
    {
        Officer.charisma = .83f + .15f * G;
        Profile.caution = .62f + .25f * G;
        Profile.aggression = .43f - .20f * G;
    }
    else
    {
        Officer.charisma = .49f + .21f * G;
        Profile.caution = .36f - .20f * G;
        Profile.aggression = .67f + .25f * G;
    }
    Profile.initiative = FMath::Max(Profile.initiative, .7f);
    Profile.charisma = Officer.charisma;
    Profile.leadership = Officer.charisma;
    Profile.tactical_skill = .67f + .24f * G;
    Officer.tactic_mind = Profile.tactical_skill;

    const bool bDeliberate = RankRating >= 5;
    const battle::Tick Rating = RankRating % 5;
    switch (Rank)
    {
    case Level::Regiment:
        Officer.lead_dist = FMath::Max(2.f, Officer.lead_dist + (bDeliberate ? 3.f + 5.f * R : -2.f - 3.f * R));
        Profile.minimum_commitment_ticks = bDeliberate ? 11 + Rating : 2 + Rating;
        break;
    case Level::Group:
    case Level::Part:
        Profile.command_radius_m = Profile.command_radius_m.value_or(75.f) +
            (bDeliberate ? 9.f + 15.f * R : -4.f - 7.f * R);
        Profile.minimum_commitment_ticks = bDeliberate ? 15 + 2 * Rating : 3 + Rating;
        break;
    case Level::Army:
        Profile.command_radius_m = Profile.command_radius_m.value_or(90.f) +
            (bDeliberate ? 10.f + 20.f * R : -4.f - 9.f * R);
        Profile.reevaluation_ticks = bDeliberate ? (bAdaptive ? 112 : 45) + 2 * Rating
            : QuickArmyInterval(bAdaptive) - 4 + Rating;
        break;
    }
}

FColor Rgb(uint32 Hex)
{
    return FColor(uint8(Hex >> 16), uint8(Hex >> 8), uint8(Hex));
}

int32 PortraitEra(FStringView EraId)
{
    if (EraId == TEXT("ancient"))
        return 0;
    if (EraId == TEXT("sengoku"))
        return 2;
    if (EraId == TEXT("napoleonic"))
        return 3;
    if (EraId == TEXT("great_northern_war"))
        return 4;
    return 1;
}

const FHistoricalCommander& ArmyPortrait(int32 Era, bool bOpposingTeam)
{
    // A bounded set of generic portraits, in pairs for the two armies.
    static const FHistoricalCommander Armies[][2] = {
        {{nullptr, EHeadwear::Helmet,  0x39494D, 0x355061, 0x443125, 0xBA8566, true},
         {nullptr, EHeadwear::Diadem,  0x634B44, 0x7E4837, 0x3A2B29, 0xD2A27E, false}},
        {{nullptr, EHeadwear::Helmet,  0x355061, 0x41516D, 0x4D362D, 0xCFA784, true},
         {nullptr, EHeadwear::Helmet,  0x593D43, 0x823E3E, 0x745042, 0xE1B998, false}},
        {{nullptr, EHeadwear::Kabuto,  0x324646, 0x335463, 0x282426, 0xC49372, true},
         {nullptr, EHeadwear::Kabuto,  0x583B36, 0x7C4A42, 0x262626, 0xD7A985, false}},
        {{nullptr, EHeadwear::Bicorne, 0x34485F, 0x324963, 0x594236, 0xD8AC85, false},
         {nullptr, EHeadwear::Bicorne, 0x584941, 0x844241, 0x706457, 0xE2B99A, false}},
    };
    return Armies[FMath::Min(Era, 3)][bOpposingTeam ? 1 : 0];
}

UTexture2D* CreatePortrait(const FHistoricalCommander& Commander)
{
    constexpr int32 Size = 64;
    TArray<FColor> Pixels;
    Pixels.SetNumUninitialized(Size * Size);
    const FColor Ink = Rgb(0x252128), Gold = Rgb(0xD0AA60), LightGold = Rgb(0xEDCD8D);
    const FColor Skin = Rgb(Commander.Skin), Hair = Rgb(Commander.Hair), Uniform = Rgb(Commander.Uniform);
    const FColor SkinShadow(Skin.R * 3 / 4, Skin.G * 3 / 4, Skin.B * 3 / 4);
    const FColor Ground = Rgb(Commander.Background);
    const auto Oval = [](float X, float Y, float Cx, float Cy, float Rx, float Ry)
    {
        const float Dx = (X - Cx) / Rx;
        const float Dy = (Y - Cy) / Ry;
        return Dx * Dx + Dy * Dy <= 1.f;
    };
    for (int32 Y = 0; Y < Size; ++Y)
        for (int32 X = 0; X < Size; ++X)
        {
            const int32 Grain = (13 * X + 11 * Y + (X * Y) % 7) % 9;
            FColor Color(Ground.R + Grain, Ground.G + Grain, Ground.B + Grain);
            if (Oval(X, Y, 32, 29, 26, 26))
                Color = FColor(Ground.R + 12 + Grain, Ground.G + 12 + Grain, Ground.B + 12 + Grain);
            if (Oval(X, Y, 32, 65, 27, 23))
                Color = Uniform;
            if (Y >= 47 && FMath::Abs(X - 32) <= (Y - 45) / 2)
                Color = X < 32 ? Gold : LightGold;
            if (Y >= 54 && ((X == 44 && Y == 56) || (X == 46 && Y == 59) || (X == 48 && Y == 56)))
                Color = LightGold;
            if (X >= 26 && X <= 37 && Y >= 37 && Y <= 48)
                Color = X < 32 ? Skin : SkinShadow;
            if (Oval(X, Y, 32, 29, 13, 17))
                Color = Hair;
            if (Oval(X, Y, 32, 33, 10, 13) || Oval(X, Y, 22, 33, 2, 4) || Oval(X, Y, 42, 33, 2, 4))
                Color = X <= 34 ? Skin : SkinShadow;
            if (Commander.bBeard && Y >= 38 && Oval(X, Y, 32, 39, 10, 8))
                Color = Hair;
            if ((Y == 30 || Y == 32) && ((X >= 25 && X <= 28) || (X >= 35 && X <= 38)))
                Color = Ink;
            if (X == 32 && Y >= 33 && Y <= 37)
                Color = SkinShadow;
            if (Y == 40 && X >= 30 && X <= 34)
                Color = Rgb(0x754F42);
            switch (Commander.Hat)
            {
            case EHeadwear::Hair:
                if (Oval(X, Y, 32, 20, 12, 6) || (X < 27 && Oval(X, Y, 24, 24, 3, 7)))
                    Color = Hair;
                break;
            case EHeadwear::Diadem:
                if (Oval(X, Y, 32, 20, 13, 8))
                    Color = Y > 20 ? Gold : Hair;
                if (Y == 22 && X % 4 == 0 && X > 20 && X < 44)
                    Color = LightGold;
                break;
            case EHeadwear::Helmet:
                if (Y <= 26 && Oval(X, Y, 32, 23, 14, 12))
                    Color = Y >= 23 ? LightGold : Gold;
                if ((X == 21 || X == 42) && Y >= 25 && Y <= 36)
                    Color = Gold;
                break;
            case EHeadwear::Turban:
                if (Oval(X, Y, 32, 19, 16, 9))
                    Color = (Y + X / 4) % 5 < 2 ? LightGold : Rgb(0xBEB196);
                if (Oval(X, Y, 33, 19, 2, 3))
                    Color = Uniform;
                break;
            case EHeadwear::Crown:
                if (X >= 18 && X <= 46 && Y <= 22 && Y >= 12 + FMath::Abs(X % 7 - 3))
                    Color = Y >= 20 ? LightGold : Gold;
                if (Y == 19 && X % 7 == 4 && X > 18 && X < 46)
                    Color = Uniform;
                break;
            case EHeadwear::Bicorne:
                if ((Y >= 12 - FMath::Abs(X - 31) / 5 && Y <= 24 - FMath::Abs(X - 31) / 6 &&
                    FMath::Abs(X - 31) <= 23) || Oval(X, Y, 32, 17, 13, 7))
                    Color = Y == 23 - FMath::Abs(X - 31) / 6 ? Gold : Ink;
                if (Oval(X, Y, 43, 18, 3, 3))
                    Color = Gold;
                break;
            case EHeadwear::Shako:
                if (Y >= 10 && Y <= 26 && FMath::Abs(X - 32) <= 11 + (26 - Y) / 6)
                    Color = Y == 11 || Y == 24 ? Gold : Ink;
                if (Oval(X, Y, 32, 20, 3, 3))
                    Color = LightGold;
                if (Oval(X, Y, 27, 10, 2, 5))
                    Color = Uniform;
                break;
            case EHeadwear::Tricorne:
                if (Y >= 12 + FMath::Abs(X - 32) / 3 && Y <= 26 && FMath::Abs(X - 32) <= 19)
                    Color = Y == 25 ? Gold : Ink;
                if (Oval(X, Y, 39, 22, 3, 3))
                    Color = LightGold;
                break;
            case EHeadwear::Kabuto:
                if (Y <= 25 && Oval(X, Y, 32, 22, 13, 12))
                    Color = Y < 20 ? Ink : Uniform;
                if (Y >= 23 && Y <= 36 && FMath::Abs(X - 32) >= 10 && FMath::Abs(X - 32) <= 17)
                    Color = Y % 4 == 0 ? Gold : Uniform;
                if (FMath::Abs(X - 32) <= 8 && Y == 10 + FMath::Abs(FMath::Abs(X - 32) - 6))
                    Color = LightGold;
                break;
            }
            if (X == 1 || Y == 1 || X == Size - 2 || Y == Size - 2)
                Color = Gold;
            Pixels[Y * Size + X] = Color;
        }

    const TConstArrayView64<uint8> Bytes(reinterpret_cast<const uint8*>(Pixels.GetData()),
        int64(Pixels.Num()) * sizeof(FColor));
    UTexture2D* Texture = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8, NAME_None, Bytes);
    if (Texture)
    {
        Texture->Filter = TF_Nearest;
        Texture->NeverStream = true;
        Texture->UpdateResource();
    }
    return Texture;
}

UTexture2D* FixedPortrait(const FHistoricalCommander& Commander)
{
    static TMap<const FHistoricalCommander*, TStrongObjectPtr<UTexture2D>> Cache;
    auto& Texture = Cache.FindOrAdd(&Commander);
    if (!Texture)
        Texture.Reset(CreatePortrait(Commander));
    return Texture.Get();
}

UTexture2D* OfficerPortrait(FStringView EraId, bool bOpposingTeam, Level Rank, uint64 Seed)
{
    const int32 Era = PortraitEra(EraId);
    const auto& Army = ArmyPortrait(Era, bOpposingTeam);
    if (Rank == Level::Army)
        return FixedPortrait(Army);

    constexpr uint32 Skin[] = {0xE2B89A, 0xD5A27E, 0xB77F5D, 0xA86D4E, 0x916249, 0x774D39};
    constexpr uint32 Hair[] = {0x342827, 0x5C4231, 0x77604A, 0x3C3734};
    constexpr int32 VariantCount = 12;
    // Bound the number of small textures across all generated battles.
    static TStrongObjectPtr<UTexture2D> Cache[5][2][2][VariantCount];
    const int32 Variant = int32(Seed % VariantCount);
    auto& Texture = Cache[Era][bOpposingTeam ? 1 : 0][Rank == Level::Regiment ? 0 : 1][Variant];
    if (!Texture)
    {
        auto Outfit = Army;
        Outfit.Skin = Skin[Variant % UE_ARRAY_COUNT(Skin)];
        Outfit.Hair = Hair[(Variant / 2) % UE_ARRAY_COUNT(Hair)];
        Outfit.bBeard = Era < 3 && Variant % 3 != 0;
        if (Era == 3 && Rank == Level::Regiment)
            Outfit.Hat = EHeadwear::Shako;
        else if (Era == 4)
            Outfit.Hat = EHeadwear::Tricorne;
        else if (Era == 0)
            Outfit.Hat = Variant % 3 == 1 ? EHeadwear::Hair : EHeadwear::Helmet;
        Texture.Reset(CreatePortrait(Outfit));
    }
    return Texture.Get();
}
}

bool HasNamePool(const FString& Era, const FString& Faction)
{
    return FindPool(Era, Faction) != nullptr;
}

void Apply(battle::Scenario& Scenario, const FMissionPreset& Preset)
{
    if (!Scenario.command_setup)
        return;

    auto& Hierarchy = Scenario.command_setup->hierarchy;
    const auto Historic = std::ranges::find(HistoricalBattles, Preset.HistoricalIdentity, &FHistoricalBattle::Mission);
    battle::command::NodeId HistoricalGroup;
    if (Historic != std::end(HistoricalBattles) && Historic->BlueGroup != NoCommander)
    {
        const auto Mounted = std::ranges::find_if(Hierarchy.nodes, [&](const auto& Node)
        {
            return Node.level == Level::Group && Node.team == 0 && IsMountedGroup(Scenario, Node);
        });
        if (Mounted != Hierarchy.nodes.end())
            HistoricalGroup = Mounted->id;
    }
    TMap<uint64, battle::UnitSpec*> Units;
    Units.Reserve(int32(Scenario.units.size()));
    for (auto& Unit : Scenario.units)
        Units.Add(Unit.id.value, &Unit);
    const FString* Factions[] = {&Preset.Era.BlueFaction, &Preset.Era.OrangeFaction};
    const FNamePool* Pools[] = {FindPool(Preset.Era.Id, *Factions[0]), FindPool(Preset.Era.Id, *Factions[1])};
    for (const auto& Node : Hierarchy.nodes)
    {
        const auto Officer = std::ranges::find(Scenario.officers, Node.commander, &battle::OfficerSpec::id);
        const auto Profile = std::ranges::find(Hierarchy.officers, Node.commander, &battle::command::OfficerProfile::officer);
        if (Officer == Scenario.officers.end() || Profile == Hierarchy.officers.end())
            continue;
        auto* Unit = Units.FindRef(Officer->unit.value);
        if (!Unit)
            continue;
        const int32 Team = Node.team == 0 ? 0 : 1;
        ECommander Identity = NoCommander;
        if (Historic != std::end(HistoricalBattles))
        {
            if (Node.level == Level::Army)
                Identity = Team == 0 ? Historic->Blue : Historic->Orange;
            if (HistoricalGroup && HistoricalGroup == Node.id)
                Identity = Historic->BlueGroup;
        }
        const auto* Named = Identity == NoCommander ? nullptr : &Historical[Identity];
        const uint64 Seed = ProfileSeed(Preset, *Factions[Team], Node);
        const FString Name = Named ? FString(Named->Name) : SeededName(Pools[Team], Seed);
        Unit->name = TCHAR_TO_UTF8(*Name);
        ApplyTraits(*Unit, *Officer, *Profile, Node.level,
            Named ? Named->Personal : uint8(Seed % 10),
            Named ? Named->General : uint8((Seed >> 16) % 10),
            Named ? Named->Rank : uint8((Seed >> 32) % 10),
            Scenario.rules.adaptive_battle);
    }
}

FCommanderCard Describe(const battle::World& World, battle::OfficerId OfficerId, FStringView EraId)
{
    FCommanderCard Card;
    const auto Officer = std::ranges::find(World.officers, OfficerId, &battle::OfficerSpec::id);
    if (Officer == World.officers.end())
        return Card;
    const auto* Unit = battle::FindUnit(World, Officer->unit);
    if (!Unit)
        return Card;
    Card.Name = UTF8_TO_TCHAR(Unit->name.c_str());
    const auto* Hierarchy = World.command_state ? &World.command_state->hierarchy
        : World.scenario.command_setup ? &World.scenario.command_setup->hierarchy : nullptr;
    const battle::command::OfficerProfile* Profile = nullptr;
    Level Rank = Level::Regiment;
    if (Hierarchy)
    {
        const auto Node = std::ranges::find(Hierarchy->nodes, OfficerId, &battle::command::CommandNode::commander);
        if (Node != Hierarchy->nodes.end())
            Rank = Node->level;
        Profile = battle::command::FindProfile(*Hierarchy, OfficerId);
    }
    Card.RoleIcon = Rank == Level::Army ? TEXT("★") : Rank == Level::Regiment ? TEXT("◆") : TEXT("▲");
    if (const auto* Appearance = ByName(Card.Name))
        Card.Portrait = FixedPortrait(*Appearance);
    else
        Card.Portrait = OfficerPortrait(EraId, battle::UnitTeam(World, *Unit) != 0, Rank,
            Mix(OfficerId.value ^ uint64(FCrc::StrCrc32(*Card.Name))));
    if (!Profile)
        return Card;
    const FNumberFormattingOptions OneDecimal = FNumberFormattingOptions().SetMaximumFractionalDigits(1);
    const auto Number = [&OneDecimal](float Value) { return FText::AsNumber(Value, &OneDecimal); };
    const auto Initial = std::ranges::lower_bound(World.scenario.units, Officer->unit, {}, &battle::UnitSpec::id);
    const auto& Starting = Initial != World.scenario.units.end() && Initial->id == Officer->unit
        ? *Initial : static_cast<const battle::UnitSpec&>(*Unit);
    const FText Personal = Starting.armour > 0.f ?
        FText::Format(NSLOCTEXT("RegimentCommanders", "Sturdy",
            "Стойкий: + запас здоровья {0} · − темп {1} м/с"), Number(Starting.health), Number(Starting.speed)) :
        FText::Format(NSLOCTEXT("RegimentCommanders", "Swift",
            "Подвижный: + темп {0} м/с · − запас здоровья {1}"), Number(Starting.speed), Number(Starting.health));
    const FText General = Profile->aggression < .6f ?
        FText::Format(NSLOCTEXT("RegimentCommanders", "Inspiring",
            "Вдохновитель · харизма {0}% · осторожнее контратакует"), Number(100.f * Officer->charisma)) :
        FText::Format(NSLOCTEXT("RegimentCommanders", "Decisive",
            "Решительный · харизма {0}% · охотнее контратакует"), Number(100.f * Officer->charisma));
    FText Role;
    switch (Rank)
    {
    case Level::Regiment:
        Role = FText::Format(Profile->minimum_commitment_ticks <= QuickCommitmentMaximum ?
            NSLOCTEXT("RegimentCommanders", "QuickRegiment", "Быстрый манёвр: + смена замысла за {0} тактов · − поддержка до {1} м") :
            NSLOCTEXT("RegimentCommanders", "RegimentAnchor", "Опора полка: + поддержка до {1} м · − смена замысла за {0} тактов"),
            FText::AsNumber(Profile->minimum_commitment_ticks), Number(Officer->lead_dist));
        break;
    case Level::Group:
    case Level::Part:
        Role = FText::Format(Profile->minimum_commitment_ticks <= QuickCommitmentMaximum ?
            NSLOCTEXT("RegimentCommanders", "QuickGroup", "Быстрый приказ: + смена замысла за {0} тактов · − обзор до {1} м") :
            NSLOCTEXT("RegimentCommanders", "GroupObserver", "Наблюдатель: + обзор до {1} м · − смена замысла за {0} тактов"),
            FText::AsNumber(Profile->minimum_commitment_ticks), Number(Profile->command_radius_m.value_or(0.f)));
        break;
    case Level::Army:
        Role = FText::Format(Profile->reevaluation_ticks <= QuickArmyInterval(World.scenario.rules.adaptive_battle) ?
            NSLOCTEXT("RegimentCommanders", "QuickArmy", "Быстрый штаб: + оценка через {0} тактов · − обзор до {1} м") :
            NSLOCTEXT("RegimentCommanders", "BroadArmy", "Дальний обзор: + обзор до {1} м · − оценка через {0} тактов"),
            FText::AsNumber(Profile->reevaluation_ticks), Number(Profile->command_radius_m.value_or(0.f)));
        break;
    }
    Card.Effects = FText::Format(NSLOCTEXT("RegimentCommanders", "CardEffects", "{0}\n{1}\n{2}"),
        Personal, General, Role);
    Card.TacticalSkill = FText::Format(NSLOCTEXT("RegimentCommanders", "TacticalSkill", "Навык тактики · {0}"),
        FText::AsPercent(Profile->tactical_skill));
    return Card;
}
}
