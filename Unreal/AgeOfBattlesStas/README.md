# Эпоха битв · Unreal Engine

Игровой проект находится в этой папке. Исходники симуляции лежат в [BattleCore](../../BattleCore). Тестовая Windows-сборка доступна на [WargamesRealms](https://wargamesrealms.ru/ageofbattles/).

## Сборка для Windows

Понадобятся Unreal Engine 5.8, Visual Studio 2022 с компонентами C++ и CMake. Из корня репозитория соберите обе библиотеки BattleCore:

```powershell
cmake -S BattleCore -B BattleCore/out/build/ue-msvc -G 'Visual Studio 17 2022' -A x64
cmake --build BattleCore/out/build/ue-msvc --config Release --target battle_core battle_io
```

Откройте `AgeOfBattlesStas.uproject` и запустите `/Game/Battle/Maps/RegimentField`. Для моделей на текстурном атласе добавьте через Fab пакет **Free LOWPO | Mercenary Company | Low Poly Characters & Weapons**. Ожидаемый путь пакета — `Content/Fab/Free_LOWPO__Mercenary_Company___Low_Poly_Characters___Weapons`. Пакеты сторонних авторов устанавливаются отдельно.

## Лендинг

Исходники лежат в `Landing`. Команда `npm run build` генерирует `Landing/dist`. Для выпуска передайте текущий коммит в переменной `AOB_RELEASE_COMMIT` и разместите установщик из `release.json` рядом с `index.html`. Предыдущий установщик остаётся доступен по своей прямой ссылке.
