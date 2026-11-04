# Skins

![DUST](https://img.shields.io/badge/DUST-0.8.4-1f6feb) ![MelonLoader](https://img.shields.io/badge/MelonLoader-0.7.x-8957e5) ![.NET](https://img.shields.io/badge/.NET-6.0-512bd4) ![Version](https://img.shields.io/badge/version-0.6.0-2ea043)

**🇬🇧 English version is below — [jump to English](#english)**

> Свои модели для персонажей DUST — например, Kobold-ковбой.

## Что умеет

- **Новый вид** — меняет модель твоего персонажа на скин из файла
- **Свой скин для каждого класса** — в меню видны классы для ПК (Scarab и Drone), мод запоминает выбор и надевает его сам
- **Цвета игры остаются** — цвет команды, вспышка от попадания и оружие работают как раньше
- **Видно другим** — игроки с этим модом видят твой скин, а ты — их, и на ПК, и на Quest

<details>
<summary>Подробнее</summary>

- Скины — это файлы `.skin` в папке игры `UserData\Skins`. Положи туда файл, и скин появится в списке.
  Скины **Cowboy** для Kobold, **Dark Knight** для Sparrow, **Enforcer** для Roller, **Gear** для Reaper, **Code Breaker** для Seraph, **Ninja** для Grappler, **Hound** для Assassin и **Sunset** для Crossbolt идут вместе с модом и появятся там сами.
- Kobold, Sparrow, Roller, Reaper, Seraph, Grappler, Assassin, Crossbolt и другие классы, кроме Scarab и Drone, — для VR. На ПК их нет в меню,
  но их скины ты увидишь на игроках в шлеме, у которых стоит мод.
- Чтобы другой игрок увидел твой скин, у него должен стоять этот мод и лежать тот же файл скина.
  Игроки без мода видят обычного персонажа — играть вместе это не мешает.
- Сделать свой скин можно в Unity-проекте `Unity\` этого репозитория: положи модель в
  `Assets\Skins\<Имя скина>\KOBOLD.fbx` и нажми **Tools → Skins → Build All**. Объекты в модели называй
  как детали персонажа в игре (`Head`, `Body`, `Left Hand`, `LH Carbine`…), а менять в Blender можно
  только саму форму, не положение объектов. Чтобы убрать деталь игры (например, `BodyHipSegment`),
  впиши её имя в файл `hide.txt` рядом с моделью. Скин соберётся сразу для ПК и для Quest.

</details>

## Управление

| Клавиша | Что делает |
|---|---|
| F2 | Открыть или закрыть меню скинов (слева сверху) |
| ↑ ↓ | В меню: выбрать класс |
| ← → | В меню: сменить скин (после последнего — обычный вид) |
| Ctrl+F11 | Записать устройство всех классов в `UserData\Skins\Dumps` (для тех, кто делает скины) |

## Установка

1. Скачай [MelonLoader](https://github.com/LavaGang/MelonLoader/releases) 0.7.x. Запусти установщик, выбери `Dust.exe` и нажми **Install**.
2. Запусти игру один раз и закрой. Это нужно, чтобы MelonLoader всё подготовил.
3. Скачай `Skins.dll` в [Releases](../../releases/latest) и положи его в папку игры `Mods`.
4. Запусти игру. В чёрном окне консоли появится **Skins v0.6.0** — значит, всё работает!

> Красные строки про **OpenXR** в консоли — это нормально, не обращай на них внимания.

### Meta Quest (без ПК)

Мод работает и на шлеме Quest без компьютера. Компьютер нужен только один раз — чтобы поставить мод.

**Что понадобится:** шлем Quest с установленной DUST, USB-кабель и компьютер с Windows.

1. **Включи режим разработчика.** В телефоне открой приложение **Meta Horizon** → **Menu** → **Devices** → твой шлем →
   **Headset settings** → **Developer mode** и включи его. Если приложение просит создать организацию разработчика —
   создай её на [developers.meta.com](https://developers.meta.com/horizon/manage/organizations/create/), это бесплатно.
2. **Подключи шлем к компьютеру** кабелем и надень шлем. Там появится окно «Allow USB debugging?» —
   поставь галочку **Always allow from this computer** и нажми **Allow**.
3. **Скачай QuestPatcher:** [QuestPatcher-windows-standalone.zip](https://github.com/Lauriethefish/QuestPatcher/releases/latest/download/QuestPatcher-windows-standalone.zip). Распакуй его и запусти `QuestPatcher.exe`.
4. **Выбери DUST.** Сначала QuestPatcher настроен на Beat Saber. Открой вкладку **Tools & Options** → **Change App**
   и выбери `org.VRDojo.Dust`.
5. **Пропатчи игру.** Вкладка **Patching** → в настройках выбери загрузчик модов **Scotland2** → нажми **Patch my app!**
   и подожди пару минут. Кабель в это время не отключай!
6. **Скачай мод:** [Skins.qmod](https://github.com/LolegRus/Skins/releases/latest/download/Skins.qmod) — скачивание начнётся сразу.
7. **Поставь мод:** перетащи файл `.qmod` прямо в окно QuestPatcher. Мод появится во вкладке **Mods** и сразу включится.
8. **Готово!** Отключи кабель, надень шлем и запусти DUST.

> Если DUST обновится из магазина, моды перестанут работать. Просто повтори шаг 5 — сами моды ставить заново не нужно.


> На Quest меню открывается нажатием **левого стика**: над левой рукой появится список классов.
> Наведи правый контроллер на `<` или `>` у класса и нажми курок. Скины Cowboy, Dark Knight, Enforcer, Gear, Code Breaker, Ninja, Hound и Sunset уже лежат в моде.

## Сборка из исходников

Нужно, только если хочешь поменять код. Понадобятся .NET SDK (6.0 или новее) и установленная DUST.
Если игра лежит не в папке по умолчанию, укажи, где она:

```
dotnet build Skins/Skins.csproj -c Release -p:DustPath="D:\Games\Dust 0.8.4"
```

Готовая DLL сама скопируется в папку игры `Mods`.

---

## English

> Your own models for DUST characters — like a cowboy Kobold.

## What it does

- **New look** — swaps your character's model for a skin from a file
- **A skin for every class** — the menu shows the PC classes (Scarab and Drone), the mod remembers your pick and puts it on by itself
- **Game colors stay** — team color, the hit flash and the weapons work just like before
- **Others see it** — players with this mod see your skin, and you see theirs, both on PC and on Quest

<details>
<summary>More details</summary>

- Skins are `.skin` files in the game's `UserData\Skins` folder. Drop a file there and the skin shows up in the list.
  The **Cowboy** skin for Kobold, the **Dark Knight** skin for Sparrow, the **Enforcer** skin for Roller, the **Gear** skin for Reaper, the **Code Breaker** skin for Seraph, the **Ninja** skin for Grappler, the **Hound** skin for Assassin and the **Sunset** skin for Crossbolt come with the mod and show up there by themselves.
- Kobold, Sparrow, Roller, Reaper, Seraph, Grappler, Assassin, Crossbolt and the other classes except Scarab and Drone are for VR. On PC they are not in the menu,
  but you see their skins on headset players who have the mod.
- For another player to see your skin, they need this mod and the same skin file.
  Players without the mod see the normal character — you can still play together.
- You can make your own skin in the `Unity\` project of this repository: put the model into
  `Assets\Skins\<Skin name>\KOBOLD.fbx` and press **Tools → Skins → Build All**. Name the objects in the model
  like the character's parts in the game (`Head`, `Body`, `Left Hand`, `LH Carbine`…), and in Blender change
  only the shapes, not where the objects are. To remove a part of the game (like `BodyHipSegment`),
  write its name into a `hide.txt` file next to the model. The skin is built for both PC and Quest at once.

</details>

## Controls

| Key | What it does |
|---|---|
| F2 | Open or close the skin menu (top left) |
| ↑ ↓ | In the menu: pick a class |
| ← → | In the menu: change the skin (after the last one — the normal look) |
| Ctrl+F11 | Write how all classes are built into `UserData\Skins\Dumps` (for skin makers) |

## Installation

1. Get [MelonLoader](https://github.com/LavaGang/MelonLoader/releases) 0.7.x. Run its installer, pick `Dust.exe` and press **Install**.
2. Start the game once, then close it. MelonLoader needs this to set itself up.
3. Download `Skins.dll` from [Releases](../../releases/latest) and put it into the game's `Mods` folder.
4. Start the game. In the black console window you should see **Skins v0.6.0** — that means it works!

> Red lines about **OpenXR** in the console are normal. Just ignore them.

### Meta Quest (no PC)

The mod also works on the Quest headset on its own. You only need a computer once — to install the mod.

**You need:** a Quest headset with DUST installed, a USB cable and a Windows computer.

1. **Turn on developer mode.** On your phone open the **Meta Horizon** app → **Menu** → **Devices** → your headset →
   **Headset settings** → **Developer mode** and switch it on. If the app asks you to create a developer organization,
   make one at [developers.meta.com](https://developers.meta.com/horizon/manage/organizations/create/) — it's free.
2. **Plug the headset into the computer** and put it on. You'll see "Allow USB debugging?" —
   tick **Always allow from this computer** and press **Allow**.
3. **Download QuestPatcher:** [QuestPatcher-windows-standalone.zip](https://github.com/Lauriethefish/QuestPatcher/releases/latest/download/QuestPatcher-windows-standalone.zip). Unzip it and run `QuestPatcher.exe`.
4. **Pick DUST.** At first QuestPatcher is set up for Beat Saber. Open the **Tools & Options** tab → **Change App**
   and choose `org.VRDojo.Dust`.
5. **Patch the game.** **Patching** tab → pick the mod loader **Scotland2** in the options → press **Patch my app!**
   and wait a couple of minutes. Don't unplug the cable while it works!
6. **Download the mod:** [Skins.qmod](https://github.com/LolegRus/Skins/releases/latest/download/Skins.qmod) — the download starts right away.
7. **Install the mod:** drag the `.qmod` file right into the QuestPatcher window. It shows up in the **Mods** tab, already on.
8. **Done!** Unplug the cable, put on the headset and start DUST.

> If DUST updates from the store, mods stop working. Just do step 5 again — you don't need to install the mods again.


> On Quest, press the **left stick** to open the menu: a list of classes shows up above your left hand.
> Point the right controller at `<` or `>` next to a class and pull the trigger. The Cowboy, Dark Knight, Enforcer, Gear, Code Breaker, Ninja, Hound and Sunset skins come with the mod.

## Building from source

Only needed if you want to change the code. You need the .NET SDK (6.0 or newer) and DUST installed.
If the game is not in the default folder, tell the build where it is:

```
dotnet build Skins/Skins.csproj -c Release -p:DustPath="D:\Games\Dust 0.8.4"
```

The finished DLL is copied into the game's `Mods` folder by itself.

---

Made by **LolegRus**
