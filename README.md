# Skins

![DUST](https://img.shields.io/badge/DUST-0.8.4-1f6feb) ![MelonLoader](https://img.shields.io/badge/MelonLoader-0.7.x-8957e5) ![Version](https://img.shields.io/badge/version-0.7.0-2ea043)

**🇬🇧 English version is below — [jump to English](#english)**

> Свои модели для персонажей DUST — например, Kobold-ковбой.

**Скачать:** [для ПК — Skins.dll](https://github.com/LolegRus/Skins/releases/latest/download/Skins.dll) · [для Quest — Skins.qmod](https://github.com/LolegRus/Skins/releases/latest/download/Skins.qmod) · 💬 [Discord](https://discord.gg/KM5YC5FjH)

## Что умеет

- **Новый вид** — меняет модель твоего персонажа на скин из файла
- **Свой скин для каждого класса** — в меню видны скины класса, за который ты играешь, у каждого своя кнопка; мод запоминает выбор и надевает его сам
- **Цвета игры остаются** — цвет команды, вспышка от попадания и оружие работают как раньше
- **Видно другим** — игроки с этим модом видят твой скин, а ты — их, и на ПК, и на Quest

<details>
<summary>Подробнее</summary>

- Скины — это файлы `.skin` в папке игры `UserData\Skins`. Положи туда файл, и скин появится в списке.
  Скины **Cowboy** для Kobold, **Dark Knight** для Sparrow, **Enforcer** для Roller, **Gear** для Reaper, **Code Breaker** для Seraph, **Ninja** для Grappler, **Hound** для Assassin, **Sunset** для Crossbolt, **Heavy Machinery** для Scarab, **Nerd** для Boulder, **Neo** для Jacket и **Detective** для Drone идут вместе с модом и появятся там сами.
- Kobold, Sparrow, Roller, Reaper, Seraph, Grappler, Assassin, Crossbolt, Boulder, Jacket и другие классы, кроме Scarab и Drone, — для VR. На ПК их нет в меню,
  но их скины ты увидишь на игроках в шлеме, у которых стоит мод.
- Чтобы другой игрок увидел твой скин, у него должен стоять этот мод и лежать тот же файл скина.
  Игроки без мода видят обычного персонажа — играть вместе это не мешает.
- Все модели скинов сделал **Cinder**.
- Меню на Quest написано шрифтом [Pixelify Sans](https://github.com/eifetx/Pixelify-Sans) (лицензия SIL OFL 1.1).

</details>

## Управление

| Клавиша | Что делает |
|---|---|
| F2 | Открыть или закрыть меню скинов (слева сверху) |
| Клик / 1–4 | В меню: надеть скин (первая кнопка — обычный вид) |
| ← → | В меню: другая страница скинов (если их больше четырёх) |

## Установка

1. Скачай [MelonLoader](https://github.com/LavaGang/MelonLoader/releases) 0.7.x. Запусти установщик, выбери `Dust.exe` и нажми **Install**.
2. Запусти игру один раз и закрой. Это нужно, чтобы MelonLoader всё подготовил.
3. Скачай `Skins.dll` в [Releases](../../releases/latest) и положи его в папку игры `Mods`.
4. Запусти игру. В чёрном окне консоли появится **Skins v0.7.0** — значит, всё работает!

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


> На Quest меню открывается нажатием **левого стика**: над левой рукой появятся скины класса, за который ты играешь.
> Наведи правый контроллер на скин и нажми курок. Скины Cowboy, Dark Knight, Enforcer, Gear, Code Breaker, Ninja, Hound, Sunset, Heavy Machinery, Nerd, Neo и Detective уже лежат в моде.

## Помощь

Что-то не работает или есть идея для скина? Пиши в нашем [Discord](https://discord.gg/KM5YC5FjH) — там же новости об обновлениях.

---

## English

> Your own models for DUST characters — like a cowboy Kobold.

**Download:** [PC — Skins.dll](https://github.com/LolegRus/Skins/releases/latest/download/Skins.dll) · [Quest — Skins.qmod](https://github.com/LolegRus/Skins/releases/latest/download/Skins.qmod) · 💬 [Discord](https://discord.gg/KM5YC5FjH)

## What it does

- **New look** — swaps your character's model for a skin from a file
- **A skin for every class** — the menu shows the skins of the class you play, each with its own button; the mod remembers your pick and puts it on by itself
- **Game colors stay** — team color, the hit flash and the weapons work just like before
- **Others see it** — players with this mod see your skin, and you see theirs, both on PC and on Quest

<details>
<summary>More details</summary>

- Skins are `.skin` files in the game's `UserData\Skins` folder. Drop a file there and the skin shows up in the list.
  The **Cowboy** skin for Kobold, the **Dark Knight** skin for Sparrow, the **Enforcer** skin for Roller, the **Gear** skin for Reaper, the **Code Breaker** skin for Seraph, the **Ninja** skin for Grappler, the **Hound** skin for Assassin, the **Sunset** skin for Crossbolt, the **Heavy Machinery** skin for Scarab, the **Nerd** skin for Boulder, the **Neo** skin for Jacket and the **Detective** skin for Drone come with the mod and show up there by themselves.
- Kobold, Sparrow, Roller, Reaper, Seraph, Grappler, Assassin, Crossbolt, Boulder, Jacket and the other classes except Scarab and Drone are for VR. On PC they are not in the menu,
  but you see their skins on headset players who have the mod.
- For another player to see your skin, they need this mod and the same skin file.
  Players without the mod see the normal character — you can still play together.
- All skin models are made by **Cinder**.
- The Quest menu uses the [Pixelify Sans](https://github.com/eifetx/Pixelify-Sans) font (SIL OFL 1.1 license).

</details>

## Controls

| Key | What it does |
|---|---|
| F2 | Open or close the skin menu (top left) |
| Click / 1–4 | In the menu: put on a skin (the first button is the normal look) |
| ← → | In the menu: another page of skins (when there are more than four) |

## Installation

1. Get [MelonLoader](https://github.com/LavaGang/MelonLoader/releases) 0.7.x. Run its installer, pick `Dust.exe` and press **Install**.
2. Start the game once, then close it. MelonLoader needs this to set itself up.
3. Download `Skins.dll` from [Releases](../../releases/latest) and put it into the game's `Mods` folder.
4. Start the game. In the black console window you should see **Skins v0.7.0** — that means it works!

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


> On Quest, press the **left stick** to open the menu: the skins of the class you play show up above your left hand.
> Point the right controller at a skin and pull the trigger. The Cowboy, Dark Knight, Enforcer, Gear, Code Breaker, Ninja, Hound, Sunset, Heavy Machinery, Nerd, Neo and Detective skins come with the mod.

## Help

Something doesn't work, or you have an idea for a skin? Write to us on [Discord](https://discord.gg/KM5YC5FjH) — updates are posted there too.

---

Made by **LolegRus** · skin models by **Cinder**
