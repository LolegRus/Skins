# Skins — для разработки

Этот файл есть только в приватной репо `LolegRus/Skins-source`. Публичная `LolegRus/Skins` — страница для игроков:
там только `README.md`, `LICENSE` и релизы (`Skins.dll` + `Skins.qmod`).

## Сборка из исходников

Понадобятся .NET SDK (6.0 или новее) и установленная DUST. Если игра лежит не в папке по умолчанию, укажи, где она:

```
dotnet build Skins/Skins.csproj -c Release -p:DustPath="D:\Games\Dust 0.8.4"
```

Готовая DLL сама скопируется в папку игры `Mods`.

## Как сделать свой скин

Unity-проект `Unity\` (Unity 6000.2.10f1): положи модель в `Assets\Skins\<Имя скина>\KOBOLD.fbx` и нажми
**Tools → Skins → Build All**. Объекты в модели называй как детали персонажа в игре (`Head`, `Body`, `Left Hand`,
`LH Carbine`…), а менять в Blender можно только саму форму, не положение объектов. Чтобы убрать деталь игры
(например, `BodyHipSegment`), впиши её имя в файл `hide.txt` рядом с моделью. Скин соберётся сразу для ПК и для Quest.

Устройство классов игры лежит в `Unity\ClassDumps`. Свежий дамп — **Ctrl+F11** в игре с модом:
он запишется в `UserData\Skins\Dumps`.

## Уголок скинов в хабе

Терминал скинов (модель Cinder: монитор с меню и голо-пьедестал, на котором крутится скин) и настоящее
зеркало рядом (`Hub.cs`, `Mirror.cs`). Модель — `Unity\Assets\Hub\terminal.fbx`, бандл собирает **Tools → Skins →
Build Hub Terminal** (`-executeMethod HubBuilder.Build`) в `Skins\Hub.bundle`, он зашивается в DLL.
Терминал в хабе крупнее модели в 1,3 раза (`Hub.TerminalScale`). Меню на экране (`TerminalMenu.cs`, макет Cinder):
слева классы со скинами, справа скины класса, «Review Skin» — показать на пьедестале; выбор — прицел на экран и ЛКМ.
У терминала и зеркала есть коллизия (MeshCollider / BoxCollider на слое пола под ними).
Голограмма на пьедестале (`Hologram.cs`) — копия префаба класса (только узлы, меши, материалы; без интерфейса на руках,
мешей урона и шляпы, которой в игре уже нет), на неё скин надевается тем же `AppliedSkin`; крутится на 360°.
Надписи — TextMeshPro шрифтом Pixelify Sans: SDF-шрифт теперь есть и в бандле ПК `MenuFont.font` (Build Menu Font).
Место — в коде: сцена `The Hub`, точка на стене и её нормаль (`Hub.MonitorAt`, `Hub.MirrorAt`); терминал и зеркало
сами встают на пол. Зеркало (`Mirror.cs`) — камера за стеклом, своё тело на время его кадра показывается; выключается `Hub.MirrorOn`.

## Публикация

README и LICENSE — общие для обеих репо: правишь здесь, потом отправляешь в публичную (из `repos\` в Git Bash):

```
./publish-public.sh Skins "Update the README"
```
