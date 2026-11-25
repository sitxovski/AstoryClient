# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
_astory_titles_selfcheck.py — headless-прогон самопроверки редактора достижений (Astory_Tools).

Что делает:
  1. `Astory.Titles.SelfCheck`          — обратная сборка title_list.bin ПОБАЙТОВО + сверка
                                          заново собранных CSV/JSON с файлами на диске;
  2. `Astory.Titles.ServerExportDryRun` — пробная выгрузка FameInfo.txt во временную папку
                                          (проводник НЕ открывается) с перечнем найденных
                                          ФАКТОМ мест на серверах;
  3. `Astory.Titles.UiSmokeTest`        — сборка всего дерева виджетов редактора и его
                                          освобождение: ловит неверные имена стилей и падения,
                                          не открывая владельцу ни одного окна.
Команды регистрирует редакторный модуль `AstoryTools`; на диск проекта ничего не пишется,
живой сервер только читается.

Побочных эффектов при импорте нет (закон `tools-no-import-side-effects`).

Запуск:  Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_astory_titles_selfcheck.py
"""
import unreal


def main():
    for cmd in ('Astory.Titles.SelfCheck', 'Astory.Titles.ServerExportDryRun', 'Astory.Titles.UiSmokeTest',
                'Astory.Tools.DumpMenu',
                'Astory.Titles.ServerAddMissing noexport'):
        unreal.log('[astory-selfcheck] >>> ' + cmd)
        unreal.SystemLibrary.execute_console_command(None, cmd)
    unreal.log('[astory-selfcheck] готово')


if __name__ == '__main__':
    main()
