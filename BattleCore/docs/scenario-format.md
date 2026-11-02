# Формат сценария, команд и сохранений

## Сценарий

Сценарий — UTF-8 JSON со `schema_version: 1`, массивами `regiments` и `units`, а также необязательными `officers`, `obstacles`, `rules` и `command_setup`. Обычные сценарии без `command_setup` работают на прежнем уровне командования.

`command_setup` задаёт дерево должностей и стартовые задачи:

```json
{
  "command_setup": {
    "hierarchy": {
      "nodes": [
        { "id": 1, "level": 0, "team": 0, "commander": 1, "children": [2] },
        { "id": 2, "parent": 1, "level": 1, "team": 0, "commander": 2, "children": [3] },
        { "id": 3, "parent": 2, "level": 2, "team": 0, "commander": 3, "children": [4] },
        { "id": 4, "parent": 3, "level": 3, "team": 0, "commander": 4, "regiment": 1 }
      ],
      "officers": [
        { "officer": 1, "command_radius_m": 60 },
        { "officer": 2, "command_radius_m": 60 },
        { "officer": 3, "command_radius_m": 60 },
        { "officer": 4, "command_radius_m": 60 }
      ],
      "support": []
    },
    "missions": [
      { "id": 1, "recipient": 1, "action": 2,
        "intent": { "target_regiment": 2,
                    "objective_position": { "x": 10, "y": 0, "z": 0 } } }
    ]
  }
}
```

Уровни `level`: 0 Army, 1 Part, 2 Group, 3 Regiment. Должность ссылается на существующий `OfficerId`; офицер — на существующий `UnitId`. Полковая должность дополнительно ссылается на существующий физический `RegimentId`. Идентификаторы ненулевые, уникальные в пределах своего типа и не являются индексами массива.

Числовые значения перечислений соответствуют порядку элементов C++ enum. Для `TaskKind`: 0 HoldArea, 1 MoveToArea, 2 Attack, 3 Defend, 4 Screen, 5 Support, 6 Flank, 7 Exploit, 8 Reserve, 9 Withdraw, 10 Rally, 11 Reform. Для входной `CommandKind`: 0 IssueOrder, 1 ReformRegiment, 2 AcquireControl, 3 ReleaseControl, 4 MoveControlledUnit, 5 AttackControlledUnit, 6 ExternalPoseSample, 7 IssueCommandTask. В JSON должны быть целые числа из допустимого диапазона, не строки и не дробные числа.

Пример команды армии из отдельного `--commands` JSON-файла:

```json
[
  {
    "tick": 0,
    "sequence": 1,
    "command": {
      "kind": 7,
      "command_task": {
        "recipient": 1,
        "action": 2,
        "intent": {
          "target_regiment": 2,
          "objective_position": { "x": 10, "y": 0, "z": 0 }
        }
      }
    }
  }
]
```

Для внешней задачи `recipient` должен быть корневой должностью Army. Между внутренними уровнями задачу принимает только непосредственный начальник. Неизвестные цели, чужая сторона, несуществующие должности и недопустимые поля отклоняются.

См. полный запускаемый сценарий с двумя полками и всеми четырьмя должностями: [`fixtures/command-battle.json`](../fixtures/command-battle.json).

## Продолжение и replay

Snapshot и replay используют схему 3. Snapshot записывает полное состояние боя и командования: память и фазы автоматов, задачи и донесения, расписания, очередь сообщений, выбранный payload и счётчики ID. `StateHash` учитывает это состояние. Читатель проверяет обязательные поля, целочисленные таймеры, диапазоны enum, ссылки и неизменность дерева относительно сценария.

Snapshot/replay схемы 2 мигрируются в схему 3 только для сценариев без `command_setup`, runtime без `command_state` и журнала без `IssueCommandTask`. Если в данных схемы 2 присутствует новая часть командования, запись отклоняется. Схема 1 и ниже не поддерживается. Нельзя менять номер схемы вручную.

```powershell
./out/build/debug/battle_cli.exe run --scenario fixtures/command-battle.json --commands path/to/commands.json --ticks 100 --out out/command-battle
./out/build/debug/battle_cli.exe run --resume out/command-battle/snapshot.json --ticks 100 --out out/command-resume
./out/build/debug/battle_cli.exe replay --replay out/command-battle/replay.json --ticks 100 --out out/command-replay
```

`--replay-out FILE` сохраняет исходный сценарий, seed и входные команды. Продолжение с `--resume` записывается в snapshot; преобразование загруженного snapshot в новый replay не выполняется.

## Прочие данные и legacy import

Размеры и координаты — метры, X вперёд, Y вправо, Z вверх; yaw — радианы. Каждый боец относится к одному полку; офицерское тело учитывается один раз. `rules.ranged_cohorts` по умолчанию равно 4, допустимо 1..64.

Legacy import читает `item.txt`, `soldiers.txt`, `officer.txt` и явно выбранную карту. Пустые строки и строки с `!` игнорируются. Числовые поля и суммы состава проверяются; ошибки указывают файл и строку. Исходные файлы не изменяются. Unity `(x,y,z)` преобразуется в core `(z,x,y)`; единица масштаба — метр; внутренний `RegimentId` на единицу больше legacy ID. Импортный формат не меняется.
