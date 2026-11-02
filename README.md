# Skins

![DUST](https://img.shields.io/badge/DUST-0.8.3-1f6feb) ![MelonLoader](https://img.shields.io/badge/MelonLoader-0.7.x-8957e5) ![.NET](https://img.shields.io/badge/.NET-6.0-512bd4) ![Version](https://img.shields.io/badge/version-0.1.0-2ea043)

**🇬🇧 English version is below — [jump to English](#english)**

> Свои скины для DUST. Пока в разработке.

## Что умеет

- **Скоро** — мод только начат, возможности появятся позже

## Управление

| Клавиша | Что делает |
|---|---|
| — | Пока ничего |

## Установка

1. Скачай [MelonLoader](https://github.com/LavaGang/MelonLoader/releases) 0.7.x. Запусти установщик, выбери `Dust.exe` и нажми **Install**.
2. Запусти игру один раз и закрой. Это нужно, чтобы MelonLoader всё подготовил.
3. Скачай `Skins.dll` в [Releases](../../releases/latest) и положи его в папку игры `Mods`.
4. Запусти игру. В чёрном окне консоли появится **Skins v0.1.0** — значит, всё работает!

> Красные строки про **OpenXR** в консоли — это нормально, не обращай на них внимания.

## Сборка из исходников

Нужно, только если хочешь поменять код. Понадобятся .NET SDK (6.0 или новее) и установленная DUST.
Если игра лежит не в папке по умолчанию, укажи, где она:

```
dotnet build Skins/Skins.csproj -c Release -p:DustPath="D:\Games\Dust 0.8.3"
```

Готовая DLL сама скопируется в папку игры `Mods`.

---

## English

> Custom skins for DUST. Still in development.

## What it does

- **Coming soon** — the mod has just started, features will come later

## Controls

| Key | What it does |
|---|---|
| — | Nothing yet |

## Installation

1. Get [MelonLoader](https://github.com/LavaGang/MelonLoader/releases) 0.7.x. Run its installer, pick `Dust.exe` and press **Install**.
2. Start the game once, then close it. MelonLoader needs this to set itself up.
3. Download `Skins.dll` from [Releases](../../releases/latest) and put it into the game's `Mods` folder.
4. Start the game. In the black console window you should see **Skins v0.1.0** — that means it works!

> Red lines about **OpenXR** in the console are normal. Just ignore them.

## Building from source

Only needed if you want to change the code. You need the .NET SDK (6.0 or newer) and DUST installed.
If the game is not in the default folder, tell the build where it is:

```
dotnet build Skins/Skins.csproj -c Release -p:DustPath="D:\Games\Dust 0.8.3"
```

The finished DLL is copied into the game's `Mods` folder by itself.

---

Made by **LolegRus**
