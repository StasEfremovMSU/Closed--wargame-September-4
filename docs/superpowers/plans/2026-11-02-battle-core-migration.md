# BattleCore Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Получить проверяемое без Unreal ядро боя на C++ и подготовленный контракт подключения Mass.

**Architecture:** Библиотека BattleCore хранит авторитетное состояние боя и исполняет фиксированные шаги. Консольный runner, импорт, сохранения, SVG и benchmarks работают поверх её API. Первый адаптер UE использует Mass для представления и передаёт действия игрока в ядро; перенос вычислений в процессоры Mass выполняется позднее по измерениям.

**Tech Stack:** C++20, CMake >=3.25, CTest, doctest 2.4.11; nlohmann/json 3.11.3 только для инструментов. Windows x64. UE toolchain и версия движка выбираются перед интеграцией.

**Spec:** [2026-11-02-battle-core-design.md](../specs/2026-11-02-battle-core-design.md).

Статус: реализация этапов 1–11 разрешена пользователем 2026-11-02. Этап 12 ожидает установленный Unreal.

## Global Constraints

- Будущие исходники: `D:/September 4/BattleCore/`, отдельная цель CMake внутри существующей рабочей копии. Не создавать вложенный Git-репозиторий.
- C++20, CMake >= 3.25, CTest; Windows x64. Сначала последовательная эталонная реализация.
- Ядро использует стандартную библиотеку, не включает заголовки Unity/UE, `UObject`, `FVector`, `USTRUCT`, файловый ввод-вывод и отрисовку.
- Ошибки ввода возвращаются типизированным результатом. Игровое ядро собирается без RTTI и исключений; ошибки валидации не используют `throw`.
- Старые C#-исходники, ресурсы, незакоммиченные изменения и исходные TXT не изменять в рамках переноса.
- 20 Гц, tick 50 мс; метры/секунды/радианы; X вперёд, Y вправо, Z вверх. ID 0 невалиден; ID не являются индексами и не переиспользуются в пределах боя.
- Начальная интеграция однопользовательская. Сеть, предсказание клиента и репликация — отдельная спецификация.
- Автономный benchmark измеряет CPU симуляции. Проверка GPU/FPS и совместимости UE остаётся отдельным обязательным этапом.

## Review Focus

1. Пустой/односторонний legacy-сценарий должен корректно загружаться либо возвращать диагностическую ошибку; не придумывать вторую армию — Task 2.
2. Удалённый ID и цель без противника не превращаются в индекс 99999 или доступ к чужому бойцу — Tasks 1, 5, 6.
3. Потери, уступка места и смена управления сохраняют каждого бойца ровно один раз — Tasks 3, 7.
4. Одновременные попадания и сохранение в середине полёта не дублируют смерть и не теряют событие — Tasks 6, 8.
5. Перестановка пакетной обработки/числа потоков не меняет зафиксированный результат одной сборки — Task 10.

## Карта переноса

| Unity-источник | Будущий модуль | Решение |
|---|---|---|
| `MainFolder/mainStatic.cs`, `regStat.cs`, `Warriors/UnitSoldiers.cs` | `world`, `types`, `formation`, `combat` | Разделить данные, правила и представление; сохранить понятия, исправить ошибки |
| `LoadIdTxt/*`, TXT | `io/LegacyImport`, `ScenarioJson` | Импорт с диагностикой и явными единицами |
| `BattleTable/*` | `scenario`, `formation`, CLI | Перенести данные расстановки; кнопки и Unity-сцену оставить ресурсом |
| `mapCell.cs`, `Prefabs/func.cs` | `spatial`, `navigation` | Новый индекс соседей и тесты границ/расстояний |
| `MainFolder/TactUpdate.cs`, `Arrow/*`, `Dead/*` | `simulation`, `combat`, `events` | Фиксированный шаг и упорядоченное применение событий |
| `UnitOfficer`, `OfficerStatement`, `Decision`, `NewBrains/*` | `officers`, `orders` | Один новый цикл решений вместо параллельных незавершённых ветвей |
| `UnitControl`, камеры, Animator/Renderer/Physics | будущий UE-адаптер | Сейчас реализовать только контракт передачи управления |
| `New_Arch/Core.cs`, тестовые заготовки | не переносить механически | Пустые классы не являются работающими механиками |

## Структура будущих файлов

Все пути ниже относительно `D:/September 4`; Tasks 1–11 реализованы и проверены, Task 12 ожидает установленного UE.

```text
BattleCore/
  CMakeLists.txt, CMakePresets.json, README.md
  include/battle/{Types,Scenario,World,Simulation,Formation,Spatial,Navigation,Orders,Officers,Combat,Control,Snapshot}.h
  src/{World,Simulation,Formation,Spatial,Navigation,Orders,Officers,Combat,Control,Snapshot}.cpp
  tools/{Main,LegacyImport,ScenarioJson,ReplayIo,SvgExport,Benchmark}.cpp
  tools/{LegacyImport,ScenarioJson,ReplayIo,SvgExport}.h
  tests/{TestMain,WorldTests,ImportTests,FormationTests,NavigationTests,OfficerTests,CombatTests,ControlTests,ReplayTests,ArtifactTests,BatchTests,IntegrationTests}.cpp
  fixtures/{legacy/,duel.json,archers.json,bottleneck.json}
  third_party/{doctest.h,json.hpp,licenses/,manifest.json}
  docs/{scenario-format.md,unreal-adapter-contract.md,benchmark-protocol.md}
  out/                         # ignored; generated builds/artifacts
```

## Согласованные имена интерфейсов для задач

Предлагаемый API; определения создаёт Task 1, дальнейшие задачи расширяют указанные типы, не вводя конкурирующие имена.

- Namespace `battle`; `using Tick=uint64_t`; отдельные strong ID-типы с полем `uint64_t value`.
- `Vec3 { float x,y,z; }`, `Pose { Vec3 position; float yaw; }`.
- `template<class T> struct Result { Status status; T value; std::vector<Diagnostic> diagnostics; }`; `Status::Ok/InvalidInput/NotFound/Conflict/NoPath`; `Diagnostic { std::string source; uint32_t line; std::string message; }`.
- `Scenario` — определения типов, бойцов, полков, офицеров, препятствий и параметры правил из спецификации; `Result<Scenario> ValidateScenario(Scenario input)`.
- `World` — собственное хранилище со стабильными ID. `Result<World> CreateWorld(const Scenario&,uint64_t seed)`; `WorldStats GetStats(const World&)`; `std::optional<UnitState> GetUnit(const World&,UnitId)`.
- `WorldStats { uint64_t living_units, dead_units, regiment_count, rank_and_file_count, officer_role_count; }`; последние два поля считают живых. `UnitState` содержит поля из спецификации; `RegimentState` содержит список живых ID и строй. `OfficerState` содержит `OfficerId`, `UnitId` и роль/характеристики командования; позицию и жизнь повторно не хранит.
- `Command` — tagged union IssueOrder, ReformRegiment, AcquireControl, ReleaseControl, MoveControlledUnit, AttackControlledUnit, ExternalPoseSample. Варианты несут соответственно Order; RegimentId+FormationSpec; UnitId; UnitId; UnitId+Vec3 desired_velocity; UnitId+UnitId target; UnitId+Pose+Tick. `CommandEnvelope { Tick tick; uint64_t sequence; Command command; }`.
- `SimulationOptions { uint32_t tick_ms=50; uint32_t worker_count=1; }`; `Result<TickResult> Step(World&,std::span<const CommandEnvelope>,const SimulationOptions&,const NavigationService&)`; `TickResult { Tick tick; std::vector<BattleEvent> events; }`.
- `Snapshot Capture(const World&)`; `Result<World> Restore(const Snapshot&)`; `uint64_t StateHash(const World&)`. Снимок/хеш включают tick, RNG, счётчики ID/событий, очередь будущих CommandEnvelope и использованные sequence, приказы, cooldown, полётные события и владение управлением.
- `NavigationService` — абстрактный интерфейс `virtual PathResult FindPath(const PathRequest&) const=0`; типы FormationFootprint/PathRequest/PathResult с точными полями из Task 4 объявить уже в Task 1. Task 4 добавляет grid backend, а `FlatNavigationService` даёт прямой путь для Tasks 1–3.
- `BattleEvent` — ID, tick, тип, source/target ID и результат; минимум Spawned, AttackLaunched, Hit, Miss, Died, OrderChanged, ControlChanged, CommandRejected.

## Task 1: Воспроизводимая сборка, мир и один шаг

**Files:** создать `BattleCore/CMakeLists.txt`, `CMakePresets.json`, `README.md`, `include/battle/{Types,Scenario,World,Simulation,Snapshot,Navigation}.h`, `src/{World,Simulation,Snapshot}.cpp`, `tests/{TestMain,WorldTests}.cpp`, `third_party/doctest.h` и manifest/license. Добавить только `BattleCore/out/` в корневой `.gitignore`.

**Interfaces:** производит весь базовый API выше. `Step` пока применяет пустой шаг; `FlatNavigationService` не требует карты. CTest-группа `battle.world`.

- [x] RED: тесты `world.empty`, `world.stable_ids`, `world.fixed_tick`, `world.seed_replay`: пустой мир имеет 0 живых; ID не равен индексу; после одного шага tick=1; tick_ms=25 -> InvalidInput без продвижения tick; одинаковые seed/команды дают одинаковый hash; запрос несуществующего ID возвращает nullopt. Зарегистрировать CTest.
- [x] RED: неверный формат пакета не меняет StateHash; игровой отказ одной корректной команды выдаёт CommandRejected и допускает остальные команды. Разграничить эти случаи по спецификации.
- [x] Запустить сборку тестов, зафиксировать отсутствие реализаций/падение проверок как RED. Настройку компилятора и проверку версий включить в эту задачу; не менять глобальный PATH и чужой проект.
- [x] GREEN: реализовать определения, валидацию и последовательный шаг 50 мс; ядро отдельно собирается без RTTI/исключений. Настроить пресеты `headless-debug`, `headless-release` с настраиваемым компилятором.
- [x] Проверить: `cmake --preset headless-debug`, `cmake --build --preset headless-debug`, `ctest --preset headless-debug -R battle.world --output-on-failure` из BattleCore; ожидаются 0 failed. Записать точные команды для найденных Windows-инструментов в README.
- [x] Просмотреть diff и сделать локальный commit только файлов задачи после утверждения начала реализации; не добавлять старые изменения.

## Task 2: Импорт сохранившихся данных и консольный runner

**Files:** `include/battle/Scenario.h`, `tools/{Main,LegacyImport,ScenarioJson}.{h,cpp}` (для Main только .cpp), `tests/ImportTests.cpp`, `fixtures/legacy/*`, `fixtures/duel.json`, `docs/scenario-format.md`, `third_party/json.hpp`, manifest/license, CMake.

**Interfaces:** использовать `battle::Result<Scenario> io::ImportLegacy(const std::filesystem::path& directory,const LegacyImportOptions&)` и `io::LoadScenario(const std::filesystem::path&)`. `LegacyImportOptions { std::filesystem::path map_file; float scale_m_per_unit=1.0f; }`; map_file задаётся относительно directory, выход за directory отвергается. `Scenario` имеет `schema_version=1`. CLI: `battle_cli import-legacy --source PATH --map mapрезерв.txt --out FILE`; `battle_cli run --scenario FILE --seed 42 --ticks 100 --out DIR`.

- [x] RED: `import.legacy_backup` -> 7 полков/200 рядовых/3 офицерских Unit/203 живых/1 сторона. Роли офицерских записей 2,1,4; повторяющийся legacy OfficerId=0 даёт предупреждение и три разных внутренних ID по номеру строки. Пустой map -> явная диагностика; пустые строки/комментарии пропускаются; дубли ID нормализованного JSON, неверный тип предмета и сломанные пары составов отвергаются с номером строки; новая валидная односторонняя JSON-карта допустима.
- [x] RED: направления Unity +Z -> core +X, Unity +X -> core +Y; legacy yaw pi/2 поворачивает фронт +X в +Y; проверять epsilon=1e-4. Legacy ID 0 преобразовать в отдельный внутренний ID, сохранив исходный номер в таблице импорта.
- [x] GREEN: скопировать фикстуры без изменения оригиналов, импортировать backup как явно выбранный источник карты; создать отдельный Unit для каждой офицерской строки и привязать OfficerState к нему после создания полков. Предметы суммировать по их ID из строки, не по индексу цикла; параметры точности/навыка валидировать. Создать новый явно синтетический duel 100 против 100 (всего 200 Unit, роли назначаются существующим бойцам). Параметры боя первой версии задать в JSON, не прятать в коде.
- [x] Проверить `ctest --preset headless-debug -R battle.import --output-on-failure`; CLI import/run дают exit=0, ошибочный ввод exit=2 и не создаёт частичный сценарий.
- [x] Локальный commit файлов задачи. CLI — единственный слой, который читает/пишет файлы.

## Task 3: Строй и движение полка

**Files:** `include/battle/Formation.h`, `src/Formation.cpp`, `src/Simulation.cpp`, `tests/FormationTests.cpp`.

**Interfaces:** `std::vector<Pose> BuildFormationSlots(const FormationSpec&,Pose centre,uint32_t count)`; `FormationSpec { uint32_t columns; float spacing_m; float body_radius_m=0.3f; }`. Пакетное продвижение — `void AdvanceFormations(World&,float dt_seconds)`, с последующим разрешением контактов до применения позиций. centre — геометрический центр строя.

- [x] RED: 25 бойцов/5 колонн/spacing=2 -> 25 уникальных мест; первый ряд центрирован по Y; задние ряды идут вдоль -X; поворот pi/2 сохраняет расстояния; движение 2 м/с за 20 tick даёт 2 м (epsilon=1e-4).
- [x] RED: потеря бойца не меняет ID остальных; явное перестроение заполняет свободные места, численность равна живым; при 0 живых нет деления на 0.
- [x] RED: встречные полки останавливаются до проникновения прямоугольников; союзник уступает по order priority, затем меньшему RegimentId; обратный порядок обхода полков даёт те же позиции. Первый вариант не обязан автоматически разруливать тупик.
- [x] GREEN: line/column выражаются числом колонн; переход к новому строю использует целевые места и ограниченную скорость, не мгновенный перенос всех солдат. До препятствий использовать FlatNavigationService.
- [x] Проверить `ctest --preset headless-debug -R 'battle.(world|formation)' --output-on-failure` и run duel 200 tick.
- [x] Локальный commit.

## Task 4: Пространственный индекс и заменяемая навигация

**Files:** `include/battle/{Spatial,Navigation}.h`, `src/{Spatial,Navigation}.cpp`, `tests/NavigationTests.cpp`, `fixtures/bottleneck.json`.

**Interfaces:** `SpatialIndex::Rebuild(std::span<const UnitPosition>)`; `std::vector<UnitId> QueryRadius(Vec3 centre,float radius) const`; `UnitPosition { UnitId id; Vec3 position; }`. `FormationFootprint { float width_m,depth_m; }`; `PathRequest { Pose start; Vec3 goal; FormationFootprint footprint; }`; `PathResult { Status status; std::vector<Pose> waypoints; }`; `GridNavigationService : NavigationService`. FlatNavigationService из Task 1 использует тот же контракт.

- [x] RED: враг в своей клетке найден; края карты безопасны; расстояние между (10,10) и (13,14) равно 5 м; после движения запрос не находит старое положение; результат отсортирован по ID.
- [x] RED: 4-связная сетка с heading, прямой коридор: колонна с подходящей шириной проходит, широкий строй получает NoPath. Длинный хвост не срезает стену при повороте на 90 градусов; свободная площадка допускает поворот. Непроходимая цель и старт вне карты дают результат, а не доступ за массив.
- [x] GREEN: реализовать индекс с перестроением на tick и A* по (cell,heading) с устойчивым разрешением равенств по (cell_index,heading). Translation проверяется swept rectangle, turn — охватывающим диском по спецификации. Движение следует Pose-waypoints, не пересекает стену; смена строя — отдельная команда, не скрытый teleport. Консервативно отклонённый тесный поворот допустим и отражается в результате.
- [x] Проверить `ctest --preset headless-debug -R 'battle.(navigation|formation)' --output-on-failure`; сохранить снимки прохождения bottleneck.
- [x] Локальный commit.

## Task 5: Офицер, приказ и объяснение решения

**Files:** `include/battle/{Orders,Officers}.h`, `src/{Orders,Officers}.cpp`, `tests/OfficerTests.cpp`, `src/Simulation.cpp`.

**Interfaces:** `Status SubmitOrder(World&,const Order&)`; `Perception Observe(const World&,OfficerId,const SpatialIndex&)`; `Decision Evaluate(const Perception&,const OfficerTraits&,const Order*)`; `void ApplyDecision(World&,OfficerId,const Decision&)`. Определить Order/Perception/Decision рядом с этими функциями; поля следуют спецификации. Decision содержит действие, оценки кандидатов и причины.

- [x] RED: нет врага -> Hold; Attack с мёртвой целью -> Failed; доставленный приказ исполняется не раньше delivery_tick; меньший приоритет не перебивает больший; знаменосец/музыкант не дают повторное движение. После смерти роли 2 командование переходит к живой роли 1; умерший командир не даёт бонусов/приказов и считается одним погибшим Unit.
- [x] RED: переоценка раз в 10 tick, минимальное удержание 20 tick; исчезнувшая цель освобождает решение немедленно; выбранный ID действия не зависит от номера элемента в массиве кандидатов.
- [x] RED: полк без командира исполняет явный MoveTo и продолжает текущий приказ, но сам не выбирает наступление. Смерть без преемника включает эту политику без сброса ID и приказа.
- [x] GREEN: Utility AI + автомат исполнения Hold/MoveTo/Attack/Withdraw/Rally; в первой версии приоритет явного приказа, затем локальная инициатива. Объяснения записываются в BattleEvent/decision trace; недоступный маршрут -> Failed.
- [x] Проверить `ctest --preset headless-debug -R battle.officers --output-on-failure`; CLI duel с командами Hold/Attack выдаёт разные наблюдаемые трассы решений.
- [x] Локальный commit.

## Task 6: Ближний бой, стрелы, потери и мораль

**Files:** `include/battle/Combat.h`, `src/Combat.cpp`, `src/Simulation.cpp`, `tests/CombatTests.cpp`, `fixtures/archers.json`.

**Interfaces:** `void BuildCombatEvents(const World&,std::vector<BattleEvent>&)`; `void CommitCombatEvents(World&,std::span<const BattleEvent>)`. Хранимую очередь, счётчики событий и cooldown определить в World; Task 8 обязан сохранять их. Применить точные временные формулы baseline из спецификации, вынести параметры в Scenario.

- [x] RED: союзник/мёртвый/вне дальности не атакуется; melee damage=max(0,damage-armour); до windup урона нет, cooldown запрещает повторный запуск; p_hit монотонно убывает с расстоянием и растёт с навыком.
- [x] RED: при distance=10 м, speed=20 м/с выстрел прибывает через 10 tick; промах присутствует в журнале; два смертельных попадания дают одну смерть; все события с одинаковым due_tick применяются до определения победителя, очередь не очищается на смене тактического периода.
- [x] RED: порог потерь включает движение к rally_point и запрещает новые атаки, но сохраняет летящие стрелы. При NoPath паника сохраняется на месте. Rally требует живого командира, нахождения в rally_tolerance_m, отсутствия урона и врага в rally_safe_radius_m в течение calm_ticks; нарушение сбрасывает счётчик; успех -> Hold. Обе стороны могут потерять последних бойцов в одном tick -> ничья.
- [x] GREEN: одна функция применения урона, ordered event queue, фильтрация сторон, явный жизненный цикл схватки. Проверку урона планировать по снимку состояния начала боевой фазы, затем commit, чтобы обход сторон не давал преимущества.
- [x] Проверить `ctest --preset headless-debug -R battle.combat --output-on-failure`, run duel/archers с фиксированным seed; commit.

## Task 7: Передача управления и протокол внешнего персонажа

**Files:** `include/battle/Control.h`, `src/Control.cpp`, `src/Formation.cpp`, `tests/ControlTests.cpp`.

**Interfaces:** внутренние исполнители команд `Status AcquireControl(World&,UnitId)`; `Status ReleaseControl(World&,UnitId)`; `Status SetExternalPose(World&,UnitId,Pose,Tick)`. Внешний runner/адаптер вызывает только Step с записанными CommandEnvelope, не эти функции напрямую. Одновременно допускается один локальный управляемый боец. Режимы: FormationControlled, PlayerControlled, Returning, Dead.

- [x] RED: вселение в живого сохраняет здоровье/экипировку/cooldown/состав; в мёртвого -> NotFound; повторное вселение в того же -> Ok без дублирования; выбор другого возвращает предыдущего ИИ.
- [x] RED: формация не меняет позицию PlayerControlled; устаревший external tick отвергается; при нескольких ExternalPoseSample одного tick побеждает последний sequence; MoveControlledUnit и ExternalPoseSample одного бойца в одном tick -> Conflict. После выхода возвращение начинается из фактической позиции; смерть управляемого освобождает управление ровно один раз.
- [x] GREEN: граница шага для смены владельца движения, команды перемещения/атаки через общий CommandEnvelope; внешний ввод не может создать новую жизнь или повторно применить EventId.
- [x] Проверить `ctest --preset headless-debug -R battle.control --output-on-failure`; scripted replay делает 100 переключений без изменения общего числа бойцов.
- [x] Локальный commit. Камера, Pawn, Enhanced Input и физический ближний бой остаются этапом UE.

## Task 8: Сохранения, повторы и диагностика без движка

**Files:** `include/battle/Snapshot.h`, `src/Snapshot.cpp`, `tools/{ReplayIo.h,ReplayIo.cpp}`, `tests/ReplayTests.cpp`, `docs/scenario-format.md`.

**Interfaces:** `io::SaveSnapshot(path,const Snapshot&)`; `Result<Snapshot> io::LoadSnapshot(path)`; `io::WriteReplay(path,const Scenario&,uint64_t seed,std::span<const CommandEnvelope>)`. Snapshot имеет schema_version=1. CLI принимает `--commands FILE`, `--resume FILE`, `--snapshot-out FILE`.

- [x] RED: run 100 tick -> save -> restore -> ещё 100 равен непрерывным 200 по StateHash; повторить при летящих стрелах, pending orders и PlayerControlled; RNG, будущие команды и очереди сохранены. Записанный ExternalPoseSample воспроизводится с тем же hash; дубли sequence не исполняют атаку повторно.
- [x] RED: неизвестная версия файла, NaN/Infinity, неверные ID/ссылки и обрезанный файл дают диагностику; загрузка не меняет существующий мир при ошибке.
- [x] GREEN: явная сериализация полей, никакого dump сырых указателей/структур; лог команд и событий использует стабильные ID и tick; hash не включает адреса, padding и порядок unordered-контейнеров.
- [x] Проверить `ctest --preset headless-debug -R battle.replay --output-on-failure` и resume CLI; commit.

## Task 9: Просмотр записанных сражений сверху

**Files:** `tools/{SvgExport.h,SvgExport.cpp}`, `tests/ArtifactTests.cpp`, `tools/Main.cpp`.

**Interfaces:** `std::string io::ExportSvg(const Snapshot&,const Scenario&)`; CLI `battle_cli render --snapshot FILE --scenario FILE --out FILE.svg`.

- [x] RED: минимальная сцена содержит отдельные обозначения полков/бойцов и заданное препятствие; пустая сцена тоже даёт валидный SVG; строки имён экранируются как XML.
- [x] GREEN: показывать границы строя, направления, маршруты, стороны, живых/погибших и управляемого бойца. SVG строится по сохранённым данным, не изменяет симуляцию.
- [x] Проверить `ctest --preset headless-debug -R battle.artifacts --output-on-failure`; вывести 3 кадра bottleneck и 3 кадра боя, открыть/визуально проверить строй и координаты.
- [x] Сохранить относительные пути и пояснения результатов в README.
- [x] Локальный commit. Полный HTML replay viewer — отдельное расширение после этой задачи.

## Task 10: Нагрузочные сценарии и пакетная оптимизация

**Files:** `tools/Benchmark.cpp`, `tests/BatchTests.cpp`, `docs/benchmark-protocol.md`, изменения горячих функций только после измерений.

**Interfaces:** CLI `battle_cli bench --units 1000,5000,10000,20000 --ticks 1200 --warmup 200 --workers 1 --out DIR`; `SimulationOptions::worker_count` использует 1 как эталон. Каждый размер проверяется в режимах march/reform/bottleneck/volley/melee.

- [x] Сначала сохранить baseline Release: 3 прогона/режим, p50/p95/p99 CPU step, память, проверки соседей, события; записать CPU/compiler/build/seed, не выводить FPS рендера.
- [x] RED: для workers=1 и будущих workers=2/4 одинаковые команды/seed дают одинаковый hash; перестановка ID в пакетах и массовые смерти не меняют результат.
- [x] GREEN: оптимизировать зафиксированные узкие места — размещение данных, пространственные запросы, выделения памяти; параллелить только независимые проходы с отдельными буферами результатов и упорядоченным commit. Если выигрыш не измерен, оставить последовательный вариант.
- [x] Проверить все CTest и повторить тот же benchmark. Начальный желаемый бюджет CPU ядра для 5k/10k: p95 <=10 мс, это критерий оптимизации, а не обещание достижимости. Если бюджет не выполнен, оформить результат и следующее узкое место.
- [x] Локальный commit с отчётом; не увеличивать детализацию искусственно ради числа сущностей.

## Task 11: Контракт Mass и проверка пакетного обмена

**Files:** `docs/unreal-adapter-contract.md`, `tests/IntegrationTests.cpp`, расширения Snapshot/Control API при необходимости.

**Interfaces:** `PresentationFrame { Tick tick; std::vector<UnitPresentation> units; std::vector<BattleEvent> events; }`; `UnitPresentation { UnitId id; RegimentId regiment; Pose pose; bool alive; ControlMode control; }`; `PresentationFrame BuildPresentation(const Snapshot&)`. Реальная UE-сборка ещё не выполняется.

- [x] RED: mock adapter связывает каждый CoreUnitId с одним presentation handle; LOD switch, pooled object reuse и повторная доставка уже применённого EventId не создают нового бойца/урона; despawn не удаляет состояние из ядра.
- [x] GREEN: документировать FMass CoreUnitId fragment, ID-map, очередность Step->frame->deferred Mass updates, правила PlayerControlled и преобразование m->cm ровно один раз.
- [x] Зафиксировать владение памятью: frame — значение у потребителя; заёмные span действуют только до следующей мутации мира. Запретить хранить указатели на C++-контейнеры в UE-объектах между шагами.
- [x] Проверить `ctest --preset headless-debug -R battle.integration --output-on-failure`; размер frame и стоимость обмена измерить для 10k. Mock доказывает контракт, не совместимость с настоящим API Mass.
- [x] Локальный commit. Результат Tasks 1–11 — самостоятельная рабочая поставка без Unreal.

## Task 12: Интеграция после появления движка — отдельный рубеж

**Files:** будущий UE-host вне Unity Assets; `Plugins/RegimentBattle/Source/{BattleCoreExternal,RegimentBattleRuntime}/`, `.uplugin`, `.Build.cs`, UE automation tests. Точные пути UE-host и версия фиксируются перед этой задачей.

- [ ] Зафиксировать версию UE/Mass, поддерживаемый MSVC/SDK/runtime; пересобрать BattleCore Release/Debug соответствующим toolchain. Для первого подключения использовать static library + External module, поставляя исходники/manifest для воспроизводимой пересборки.
- [ ] RED: настоящий интеграционный тест: 100 бойцов -> 100 Mass entities, правильные оси/сантиметры; уничтожение и LOD сохраняют ID; external Pawn movement не конкурирует с ядром.
- [ ] GREEN: world subsystem ведёт фиксированный tick; Mass processors принимают пакетные кадры и создают представления; контролируемый Character привязывается к CoreUnitId. Подготовить отображение событий атаки, попадания и смерти.
- [ ] Проверить UBT/UHT, packaged build, реальную анимацию, навигацию и физику. Дальше 1k/5k/10k, обзор сверху и камера внутри схватки; измерить CPU/GPU frame и пики переключений. Бюджет 60 FPS при 1080p — цель проверки, а не результат автономного benchmark.
- [ ] Только после профиля решить, какие чистые пакетные функции исполнять непосредственно процессорами Mass и какие данные перенести в его хранилище. Выполнить миграцию с одним источником истины и сравнением с headless-эталоном. Ревью/commit отдельно от предыдущей поставки.

## Порядок исполнения и точки обсуждения

Порядок: 1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 7 -> 8 -> 9 -> 10 -> 11. Task 12 после появления UE. Каждая задача включает собственную RED/GREEN проверку и ревью; продолжать следующую только после прохождения проверок текущей.

Перед реализацией подтвердить архитектуру и предложенный упрощённый baseline боя. Детальный ручной бой при вселении, полный штабной ИИ, физические стрелы/укрытия и сеть не входят в первую автономную поставку.

Рекомендуемый способ исполнения — native по задачам с независимым ревью ключевых границ данных, боя и адаптера. При желании пользователя перейти к subagent-driven исполнению. Исполнение Tasks 1–11: native через executing-plans, с одним независимым итоговым ревью всей ветки.
