# Боевой HUD

> **For agentic workers:** Use superpowers:executing-plans for the implementation and verification steps below.

**Цель.** С первого взгляда различать офицера, полк-адресат, текущее действие и прохождение команды.

**Согласованный дизайн.** Панель приказов внизу с именем полка, офицером, переключением полка и возвратом к общему виду. Команды строя, тактики и следования образуют отдельные группы. В дереве решений краткая сводка видна в заголовке, описание и приказы подчинённым раскрываются по нажатию.

**Границы.** Существующий Slate и палитра игры. Сохранить горячие клавиши, управление камерой и правила BattleCore. Боевой HUD, включая дерево офицеров. Проверить широкое окно Play и узкий оконный режим Windows.

## Состояние приказа

- [x] В `Plugins/RegimentBattle/Source/RegimentBattleRuntime/Private/RegimentOfficerOrdersTest.cpp` закрепить сценарий `RegimentBattle.OfficerOrderPresentation`: после F на паузе видна очередь, после шага виден активный режим, в рукопашной видна причина приостановки, а прямой приказ меняется с очереди на передачу и исполнение. При смене выбранного полка очередь предыдущего не отображается.
- [x] Запустить целевую проверку на существующем поведении и зафиксировать красный результат.
- [x] В `Plugins/RegimentBattle/Source/RegimentBattleRuntime/Private/RegimentOfficerOrders.h/.cpp` представить текущую активность полка и состояние передачи по данным World и очереди, подключить срез в `Plugins/RegimentBattle/Source/RegimentBattleRuntime/Private/RegimentBattleField.cpp` и `Plugins/RegimentBattle/Source/RegimentBattleRuntime/Public/RegimentBattleField.h`.

## Компоновка

- [x] В `Plugins/RegimentBattle/Source/RegimentBattleRuntime/Private/SRegimentBattleHud.cpp` разделить управление офицером и приказы, расположить статус рядом с адресатом, сделать перенос целыми группами на меньшей ширине.
- [x] В `Plugins/RegimentBattle/Source/RegimentBattleRuntime/Private/SRegimentCommandPanel.cpp` оставить компактные сводки на свернутых строках. При раскрытии сначала показать доступные действия, затем детали и подчинённых.
- [x] Собрать RegimentBattleRuntime, запустить Unreal Automation `RegimentBattle`, проверить паузу с очередью, F, смену полка у генерала, открытие и закрытие деталей в Play.


Сборка плагина успешна. Прошли 18 проверок Unreal и контрольный прогон RegimentField. В Play проверены смена адресата, очередь и передача приказа, детали дерева и панели в полноразмерном и узком окнах.
