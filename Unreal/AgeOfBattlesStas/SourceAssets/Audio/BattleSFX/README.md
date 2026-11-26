# Звуки Age of Battles

В наборе 60 файлов WAV PCM, 48 кГц, 16 бит, импортированных как SoundWave в `/Game/Battle/Audio`. Позиционные эффекты и фон рукопашной в моно. Дальняя пороховая битва в стерео.

## Как использовать набор

- Отдельные события используют образцы мушкета, пушки, удара офицера, парирования, выпуска и попадания стрелы. В образцах попадания сохранена более поздняя часть исходных записей, после начального резкого щелчка.
- Выпуск катапульты звучит тяжелее и дольше за счёт дерева и каната. Выпуск баллисты опирается на короткий щелчок тетивы и затухание рамы. Исходные слои доступны в папке `Siege`.
- Для офицера есть по два отдельных шага на каждую комбинацию поверхности и темпа. Короткие записи марша и копыт рассчитаны на звук группы при движении.
- Для прямого воспроизведения в Unreal зациклены `Forest_Wind_Loop`, `Brook_Loop`, `Distant_Melee_Battle_Loop` и `Distant_BlackPowder_Battle`. Фон рукопашной собран из записей криков, шага, копыт, стали и ветра.
- Громкость интерфейса, команд, окружения и боя регулируется отдельно. Расстановка сигналов и баланс зависят от игрового режима и положения камеры.

## Источники и лицензии

У каждой записи лицензия [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/). Имена авторов сохранены для проверки происхождения. Дата доступа 26.11.2026.

Применены нарезка, сведение позиционных эффектов в моно, удаление инфранизких частот и выравнивание пиков с запасом. Ветер, ручей и дальняя пороховая битва собраны с перекрытием для зацикливания. В фоне рукопашной крики, движение и удары разнесены по времени, а дорожка ветра сведена с перекрытием на стыке. Строки с HQ-превью указывают на публичные MP3 с Freesound, пересохранённые в WAV для импорта.

### Удары, парирование, лук, мушкет и полевая пушка

| WAV | Длительность | Автор и запись | Полученный файл |
|---|---:|---|---|
| [Steel_Clash_01](Combat/Steel_Clash_01.wav) | 1.25 с | Fenodyrie · [sword against sword.wav](https://freesound.org/people/Fenodyrie/sounds/568790/) | Публичное HQ-превью MP3 |
| [Steel_Clash_02](Combat/Steel_Clash_02.wav) | 1.13 с | Fenodyrie · [sword against sword.wav](https://freesound.org/people/Fenodyrie/sounds/568790/) | Публичное HQ-превью MP3 |
| [Steel_Clash_03](Combat/Steel_Clash_03.wav) | 1.01 с | Fenodyrie · [sword against sword.wav](https://freesound.org/people/Fenodyrie/sounds/568790/) | Публичное HQ-превью MP3 |
| [Bow_Release_01](Combat/Bow_Release_01.wav) | 0.56 с | Ali_6868 · [Bow Release (Bow and Arrow)](https://freesound.org/people/Ali_6868/sounds/384915/) | Публичное HQ-превью MP3 |
| [Bow_Release_02](Combat/Bow_Release_02.wav) | 0.28 с | Joseph Sardin · [Archery](https://bigsoundbank.com/archery-s0549.html) | Исходник WAV |
| [Bow_Release_03](Combat/Bow_Release_03.wav) | 0.30 с | Joseph Sardin · [Archery](https://bigsoundbank.com/archery-s0549.html) | Исходник WAV |
| [Arrow_Target_01](Combat/Arrow_Target_01.wav) | 0.187 с | Joseph Sardin · [An arrow on a target](https://bigsoundbank.com/an-arrow-on-a-target-s0548.html) | Исходник WAV |
| [Arrow_Target_02](Combat/Arrow_Target_02.wav) | 0.167 с | Joseph Sardin · [An arrow on a target](https://bigsoundbank.com/an-arrow-on-a-target-s0548.html) | Исходник WAV |
| [Arrow_Quiver_01](Combat/Arrow_Quiver_01.wav) | 0.71 с | Vehicle · [sfx/arrow-grab-from-quiver-03.wav](https://opengameart.org/content/fantasy-weapons-and-apparel-sfx-library) | Авторский WAV из ZIP |
| [Musket_Shot_01](Combat/Musket_Shot_01.wav) | 3.35 с | bruno.auzet · [old musket bang.wav](https://freesound.org/people/bruno.auzet/sounds/538795/) | Публичное HQ-превью MP3 |
| [Field_Cannon_01](Combat/Field_Cannon_01.wav) | 3.55 с | Thimras · [Battle at sea / cannon_fire](https://opengameart.org/content/battle-at-sea) | Авторский OGG |
| [Field_Cannon_02](Combat/Field_Cannon_02.wav) | 3.48 с | raphavpires · [Cannon Fire](https://freesound.org/people/raphavpires/sounds/867028/) | Публичное HQ-превью MP3 |
| [Officer_Hit_01](Combat/Officer_Hit_01.wav) | 0.325 с | Joseph Sardin · [Punch #3](https://bigsoundbank.com/punches-3-s0806.html) и Vehicle · [Fantasy Weapons and Apparel SFX Library](https://lpc.opengameart.org/content/fantasy-weapons-and-apparel-sfx-library) (`boots-leather-jump-03`, `quiver-leather-squeeze-05`) | Сведение исходных WAV |
| [Officer_Parry_01](Combat/Officer_Parry_01.wav) | 0.645 с | Vehicle · [Fantasy Weapons and Apparel SFX Library](https://lpc.opengameart.org/content/fantasy-weapons-and-apparel-sfx-library) (`sword-knife-clash-04`) | Исходник WAV из ZIP |
| [Cavalry_Impact_01](Combat/Cavalry_Impact_01.wav) | 0.433 с | Joseph Sardin · `Horse_Gallop_01`, `Wood_Resonance_02`, [Punch #3](https://bigsoundbank.com/punches-3-s0806.html) и Vehicle · [Fantasy Weapons and Apparel SFX Library](https://lpc.opengameart.org/content/fantasy-weapons-and-apparel-sfx-library) (`quiver-leather-squeeze-05`) | Сведение WAV |

### Катапульта и баллиста

| WAV | Длительность | Автор и запись | Полученный файл |
|---|---:|---|---|
| [Heavy_Rope_01](Siege/Heavy_Rope_01.wav) | 1.12 с | Joseph Sardin · [Big rope](https://bigsoundbank.com/big-rope-s3426.html) | Публичный FLAC |
| [Heavy_Rope_02](Siege/Heavy_Rope_02.wav) | 1.42 с | Joseph Sardin · [Big rope](https://bigsoundbank.com/big-rope-s3426.html) | Публичный FLAC |
| [Wood_Resonance_01](Siege/Wood_Resonance_01.wav) | 0.58 с | Joseph Sardin · [Wood vibrations](https://bigsoundbank.com/wood-vibrations-s2436.html) | Публичный FLAC |
| [Wood_Resonance_02](Siege/Wood_Resonance_02.wav) | 0.52 с | Joseph Sardin · [Wood vibrations](https://bigsoundbank.com/wood-vibrations-s2436.html) | Публичный FLAC |
| [Catapult_Release_01](Siege/Catapult_Release_01.wav) | 0.960 с | Слои `Heavy_Rope_02`, `Wood_Resonance_01/02`, `Bow_Release_01` | Сведение WAV с понижением тона дерева и тетивы |
| [Ballista_Release_01](Siege/Ballista_Release_01.wav) | 0.655 с | Слои `Heavy_Rope_01/02`, `Wood_Resonance_02`, `Bow_Release_03` | Сведение WAV |

### Движение офицера, строя и конницы

| WAV | Длительность | Автор и запись | Полученный файл |
|---|---:|---|---|
| [Foot_Grass_Walk_01](Movement/Foot_Grass_Walk_01.wav) | 4.45 с | Joseph Sardin · [Steps in the Grass, Slow](https://bigsoundbank.com/steps-in-the-grass-slow-s1253.html) | Публичный FLAC |
| [Foot_Grass_Run_01](Movement/Foot_Grass_Run_01.wav) | 5.90 с | Joseph Sardin · [Running in the Tall Grass](https://bigsoundbank.com/running-in-the-tall-grass-s1843.html) | Публичный FLAC |
| [Foot_Gravel_01](Movement/Foot_Gravel_01.wav) | 7.00 с | Joseph Sardin и Axeline T. · [Footsteps on Gravels #4](https://bigsoundbank.com/footsteps-on-gravels-4-s3216.html) | Исходник FLAC |
| [Foot_Mud_01](Movement/Foot_Mud_01.wav) | 8.00 с | Joseph Sardin · [Steps in the Mud](https://bigsoundbank.com/steps-in-the-mud-s0495.html) | Исходник WAV |
| [Regiment_March_01](Movement/Regiment_March_01.wav) | 10.77 с | Joseph Sardin · [Marching in #1](https://bigsoundbank.com/marching-in-1-s3023.html) | Публичный FLAC |
| [Horse_Walk_01](Movement/Horse_Walk_01.wav) | 11.40 с | Joseph Sardin · [Horse Walking on a Path](https://bigsoundbank.com/horse-walking-on-a-path-s1854.html) | Публичный FLAC |
| [Horse_Gallop_01](Movement/Horse_Gallop_01.wav) | 12.48 с | Joseph Sardin · [Horse Gallop](https://bigsoundbank.com/horse-gallop-s0611.html) | Публичный FLAC |
| [Gear_Leather_01](Movement/Gear_Leather_01.wav) | 0.45 с | Vehicle · [sfx/quiver-leather-squeeze-05.wav](https://opengameart.org/content/fantasy-weapons-and-apparel-sfx-library) | Авторский WAV из ZIP |
| [Gear_Buckle_01](Movement/Gear_Buckle_01.wav) | 0.67 с | Vehicle · [sfx/belt-buckle-02.wav](https://opengameart.org/content/fantasy-weapons-and-apparel-sfx-library) | Авторский WAV из ZIP |
| [Regiment_March_Short](Movement/Regiment_March_Short.wav) | 1.850 с | Фрагмент `Regiment_March_01` | Нарезка WAV |
| [Horse_Walk_Short](Movement/Horse_Walk_Short.wav) | 1.890 с | Фрагмент `Horse_Walk_01` | Нарезка WAV |
| [Horse_Gallop_Short](Movement/Horse_Gallop_Short.wav) | 1.940 с | Фрагмент `Horse_Gallop_01` | Нарезка WAV |

### Короткие шаги офицера

Дубли вырезаны вокруг отдельных контактов стопы с поверхностью. На гравии и в грязи для бега использованы отдельные записи бега.

| WAV | Длительность | Автор и запись | Полученный файл |
|---|---:|---|---|
| [Foot_Grass_Walk_02](Movement/Foot_Grass_Walk_02.wav), [03](Movement/Foot_Grass_Walk_03.wav) | 0.184 / 0.236 с | Joseph Sardin · [Steps in the Grass, Slow](https://bigsoundbank.com/steps-in-the-grass-slow-s1253.html) | Публичный FLAC |
| [Foot_Grass_Run_02](Movement/Foot_Grass_Run_02.wav), [03](Movement/Foot_Grass_Run_03.wav) | 0.143 / 0.180 с | Joseph Sardin · [Running in the Tall Grass](https://bigsoundbank.com/running-in-the-tall-grass-s1843.html) | Публичный FLAC |
| [Foot_Gravel_Walk_01](Movement/Foot_Gravel_Walk_01.wav), [02](Movement/Foot_Gravel_Walk_02.wav) | 0.235 / 0.230 с | Joseph Sardin и Axeline T. · [Footsteps on Gravels #4](https://bigsoundbank.com/footsteps-on-gravels-4-s3216.html) | Исходник FLAC |
| [Foot_Gravel_Run_01](Movement/Foot_Gravel_Run_01.wav), [02](Movement/Foot_Gravel_Run_02.wav) | 0.199 / 0.186 с | Nox_Sound · [Footsteps_Mountain_Boots_Gravel_Mono.wav](https://freesound.org/people/Nox_Sound/sounds/556002/) | Публичное HQ-превью MP3 |
| [Foot_Mud_Walk_01](Movement/Foot_Mud_Walk_01.wav), [02](Movement/Foot_Mud_Walk_02.wav) | 0.306 / 0.247 с | Joseph Sardin · [Steps in the Mud](https://bigsoundbank.com/steps-in-the-mud-s0495.html) | Исходник WAV |
| [Foot_Mud_Run_01](Movement/Foot_Mud_Run_01.wav), [02](Movement/Foot_Mud_Run_02.wav) | 0.178 / 0.155 с | Luisa_Sanchez · [Run in mud](https://freesound.org/people/Luisa_Sanchez/sounds/813369/) | Публичное HQ-превью MP3 |

### Горн, барабан и сигнальная труба

| WAV | Длительность | Автор и запись | Полученный файл |
|---|---:|---|---|
| [Horn_Call_01](Signals/Horn_Call_01.wav) | 3.27 с | Alon-De-Lon · [Hunting horn tone](https://commons.wikimedia.org/wiki/File:Hunting_horn_tone.ogg) | Авторский OGG |
| [Horn_Call_02](Signals/Horn_Call_02.wav) | 3.04 с | Alon-De-Lon · [Hunting horn tone](https://commons.wikimedia.org/wiki/File:Hunting_horn_tone.ogg) | Авторский OGG |
| [Drum_Hit_01](Signals/Drum_Hit_01.wav) | 0.62 с | Joseph Sardin · [Snare drum #1](https://bigsoundbank.com/snare-drum-1-s2304.html) | Публичный FLAC |
| [Drum_Roll_01](Signals/Drum_Roll_01.wav) | 4.90 с | Joseph Sardin · [Drum Roll 1 L](https://bigsoundbank.com/drum-roll-1-l-s2402.html) | Публичный FLAC |
| [Bugle_Phrase_01](Signals/Bugle_Phrase_01.wav) | 4.45 с | Joseph Sardin · [Bugle Awake for Morning Call](https://bigsoundbank.com/bugle-awake-morning-call-s3263.html) | Публичный FLAC |

### Окружение и звуки боя вдали

| WAV | Длительность | Автор и запись | Полученный файл |
|---|---:|---|---|
| [Forest_Wind_Loop](Ambience/Forest_Wind_Loop.wav) | 30.00 с | Joseph Sardin · [Wind in the Trees](https://bigsoundbank.com/forest-wind-in-the-trees-s0904.html) | Исходник OGG |
| [Brook_Loop](Ambience/Brook_Loop.wav) | 24.00 с | Joseph Sardin · [Small stream #1](https://bigsoundbank.com/small-stream-1-s0213.html) | Исходник WAV |
| [Distant_BlackPowder_Battle](Ambience/Distant_BlackPowder_Battle.wav) | 35.00 с | alienistcog · [reenactment2.aif](https://freesound.org/people/alienistcog/sounds/125932/) | Публичное HQ-превью MP3 |
| [Charge_Shouts_01](Ambience/Charge_Shouts_01.wav) | 3.65 с | florianreichelt · [People screaming in agony when charging into battle](https://freesound.org/people/florianreichelt/sounds/563011/) | Публичное HQ-превью MP3 |
| [Distant_Melee_Battle_Loop](Ambience/Distant_Melee_Battle_Loop.wav) | 18.00 с | `Charge_Shouts_01`, `Regiment_March_01`, `Horse_Gallop_01`, [Wind in the Trees](https://bigsoundbank.com/forest-wind-in-the-trees-s0904.html) и Vehicle · [Fantasy Weapons and Apparel SFX Library](https://lpc.opengameart.org/content/fantasy-weapons-and-apparel-sfx-library) (`sword-knife-clash-04/11/19/20/26`) | Сведение записей и фильтрация для эффекта удалённости |

### Азиатский слон

| WAV | Длительность | Автор и запись | Полученный файл |
|---|---:|---|---|
| [Elephant_Asian_Call_01](Animals/Elephant_Asian_Call_01.wav) | 1.44 с | தகவலுழவன் (Info-farmer) · [Elephant voice – trumpeting](https://commons.wikimedia.org/wiki/File:Elephant_voice_-_trumpeting.ogg) | Авторский OGG |

### Интерфейс

| WAV | Длительность | Автор и запись | Полученный файл |
|---|---:|---|---|
| [Menu_Click](UI/Menu_Click.wav) | 0.10 с | Kenney · [Interface Sounds / Audio/click_001.ogg](https://opengameart.org/content/interface-sounds) | Авторский OGG из ZIP |
| [Menu_Confirm](UI/Menu_Confirm.wav) | 0.29 с | Kenney · [Interface Sounds / Audio/confirmation_001.ogg](https://opengameart.org/content/interface-sounds) | Авторский OGG из ZIP |
| [Menu_Error](UI/Menu_Error.wav) | 0.53 с | Kenney · [Interface Sounds / Audio/error_003.ogg](https://opengameart.org/content/interface-sounds) | Авторский OGG из ZIP |
| [Menu_Back](UI/Menu_Back.wav) | 0.09 с | Kenney · [Interface Sounds / Audio/back_002.ogg](https://opengameart.org/content/interface-sounds) | Авторский OGG из ZIP |
