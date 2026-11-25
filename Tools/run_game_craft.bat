@echo off
rem Copyright (c) 2026 @sitxovski (Discord)
REM ★★★ САМОТЕСТ ОКНА «СОЗДАНИЕ ПРЕДМЕТА» 76000 И МОДЕЛИ РЕМЕСЛА ПУТЁМ ИГРОКА (-game), БЕЗ ОКОН НА ЭКРАНЕ (25.09.2026, волны В0–В2).
REM   * -game            — путь игрока, а не PIE (закон ui-check-game-path-geometry);
REM   * -RenderOffscreen — окон Unreal на экране не появляется (закон no-visible-editor-windows);
REM   * БЕЗ -NullRHI     — иначе Slate не рисует, геометрия нулевая и кадров не будет.
REM   Сервера нет: самотест синтезирует 6597/6599/6601/6603/6605 и прогоняет их БОЕВЫМИ приёмниками
REM   (asda.Craft.SelfTest, AsdaCraftWindowSubsystem.cpp); исходящие 6598/6600/6602/6604 ловит узкий перехват
REM   (только свой опкод, снимается сам после первого пакета).
REM   Кадры: @@ASTORY_DEV_ROOT@@\AstoryClient\Saved\Screenshots\WindowsEditor\asda_craft_w012_*.png
REM   Строки проверок — «САМОТЕСТ-76000», итог — «САМОТЕСТ-76000 ИТОГ».
REM   ★ Волна В3 (26.09.2026): второй проход `asda.Craft.SelfTestRecipe` (через 10 с) — окно состава 77000, под-окна
REM     77100/77200, выбор стека (К-6), снимок открытых окон (К-7), гейты «ремесло открыто»; кадры w3_*.png;
REM     строки проверок — «САМОТЕСТ-77000», итог — «САМОТЕСТ-77000 ИТОГ».
REM   ★ Волна В4 (26.09.2026): третий проход `asda.Craft.SelfTestMake` (через 20 с) — окно изготовления 77500: вход из 77000,
REM     поле количества, ступени, провал на первой (К-3, Р-4), «+1 шт.», серия с ожиданием ответа, прерывание, К-1, отказы,
REM     таймаут, К-6, свитки 101/102; кадры w4_*.png; строки «САМОТЕСТ-77500», итог «САМОТЕСТ-77500 ИТОГ».
REM     Четвёртый проход `asda.Craft.SelfTestSnapshot` (через 64 с) — снимок окон: слоты 1/4/13 через разговор (ревизия В3 К-1),
REM     30035/30046/30020 и элитный магазин 6000 (С-1/С-3), закрыватели 43000 (С-2); итог «САМОТЕСТ-СНИМОК ИТОГ». Выход — у него.
REM     Префикс кадров любого прохода — параметр `prefix=` (ревизор не перетирает кадры волны).
REM   ★ Волна В5 (26.09.2026): пятый проход `asda.Craft.SelfTestW5` (через 70 с) — звуки окон-сообщений 25/4/5 ровно по одному
REM     (общая точка), команда NPC 30030 → элитный магазин 6000, слот 8 снимка (значок званий), плашка 221 после смены карты,
REM     смерть с открытым 6000; итог «САМОТЕСТ-В5 ИТОГ». Выход — у него (снимок больше не выходит).
REM     Второй аргумент — префикс кадров всех проходов (ЗНАЧЕНИЕ, без `prefix=`: cmd режет аргументы по `=`); первый
REM     аргумент «-» = без дополнительных команд: run_game_craft.bat - w5_
REM   ★ Волна В8 (26.09.2026, финальная ревизия В7): шестой проход `asda.Craft.SelfTestW8` (через 64 с) — С-2 (снимок 77500
REM     переживает Z/закрытие 76000/поздний 6601, сумка возвращается после изготовления), С-1 (портал и разрыв при открытом
REM     77500 возвращают HUD без окон снимка), С-3 (офлайн-гейт); итог «САМОТЕСТ-В8 ИТОГ». Снимок сдвинут на 76 с, В5 — на 82 с
REM     (без выхода). Седьмой проход `asda.Craft.SelfTestW8 death` (через 98 с, после смерти В5) — смерть при открытом 77500,
REM     порядок 5045 → сброс 77500; итог «САМОТЕСТ-В8-СМЕРТЬ ИТОГ». Выход — у него. Все самотесты ремесла при живом
REM     соединении с сервером отказываются (С-3).
REM   ★ W8-2 (26.09.2026): проход `death` дополнен смертью при открытом 77000 без 77500 (76000 → 77000, сумка и окно персонажа
REM     закрыты после снимка) в обоих значениях asda.Craft.FixDeathRestoreOrder; выход сдвинут внутри прохода на 6.2 с.
setlocal
call "%~dp0asda_ue_engine.bat"
set ENGINE=%ASDA_UE_EDITOR_CMD%
set PROJECT=@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject
set LOG=@@ASTORY_DEV_ROOT@@\Reports\craft\headless_craft_game.log
set EXTRA=%~1
if "%EXTRA%"=="-" set EXTRA=
set PREFIX=
if not "%~2"=="" set PREFIX=prefix=%~2
"%ENGINE%" "%PROJECT%" "/Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode" -game -AsdaNoNet -RenderOffscreen -unattended -nopause -nosplash -nosound -ResX=1280 -ResY=800 -stdout -FullStdOutLogOutput -abslog="%LOG%" -ExecCmds="%EXTRA%asda.Craft.SelfTest shot %PREFIX%, asda.Craft.SelfTestRecipe delay=10 shot %PREFIX%, asda.Craft.SelfTestMake delay=20 shot %PREFIX%, asda.Craft.SelfTestW8 delay=64 shot %PREFIX%, asda.Craft.SelfTestSnapshot delay=76, asda.Craft.SelfTestW5 delay=82, asda.Craft.SelfTestW8 death delay=98 quit"
echo === Готово. Код выхода: %ERRORLEVEL%. Лог: %LOG% ===
