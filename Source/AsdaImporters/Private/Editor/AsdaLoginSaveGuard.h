// Copyright (c) 2026 @sitxovski (Discord)
// AsdaLoginSaveGuard — защита сцены логина владельца на уровне ДВИЖКА (15.09.2026).
//
// ★ Зачем. Headless-запуск редактора (-ExecutePythonScript / -run=pythonscript) открывает стартовую карту
//   EditorStartupMap=/Game/Map/Login. Python-скрипт, не проверивший результат load_level/new_level, затем зовёт
//   save_current_level()/save_dirty_packages() — и молча пересохраняет Login.umap со светом владельца
//   (память owner-login-light-protected: «никогда и ни при каких обстоятельствах»). Проверки в Tools/*.py
//   закрывают известные скрипты, но не будущие и не драйверы в Reports — поэтому нужна одна точка в движке.
//
// ★ Механизм UE 5.8. SavePackage2.cpp:218 перед записью пакета спрашивает
//   FCoreUObjectDelegates::IsPackageOKToSaveDelegate (UObjectGlobals.h:3425, одиночный делегат);
//   false → ESavePackageResult::Error, файл на диск не пишется. Делегат уже занят редактором
//   (EditorEngine.cpp:1031 → UEditorEngine::IsPackageOKToSave, фильтр записываемых папок), поэтому мы его
//   ОБОРАЧИВАЕМ: сначала своя проверка, затем прежний обработчик.
//
// ★ Доработка по ревизии (15.09.2026):
//   - УДАЛЕНИЕ: EditorAssetLibrary.delete_asset/delete_directory идут через ObjectTools::ForceDeleteObjects →
//     CleanupAfterSuccessfulDelete → IFileManager::Delete, делегат сохранения не спрашивается. Подписка на
//     FEditorDelegates::OnAssetsCanDelete (ObjectTools.cpp:3630/3116/3459) отказывает с теми же условиями.
//     ПЕРЕИМЕНОВАНИЕ (AssetRenameManager) OnAssetsCanDelete не спрашивает — см. отчёт login_save_guard.md.
//   - Признаки автоматики: + -ExecCmds с Python-командой («py …») или «Automation …», -EXEC=<файл>, -AutomatedTests.
//     -ExecCmds без Python (stat fps и т.п.) автоматикой НЕ считается. Самотест: консоль asda.LoginGuard.SelfTestCmdLine.
//   - Разрешение уровня — ТОЛЬКО ASDA_ALLOW_LOGIN_MAP_WRITE=1; ASDA_ALLOW_LOGIN_WRITE=1 (ассеты логина) уровень не открывает.
//   - ★ Файловый сторож (AsdaLoginFileGuard.h): при действующем запрете Install ставит обёртку IPlatformFile, которая
//     отказывает в записи/удалении/перемещении файлов уровня мимо делегатов (закрывает rename_asset без редиректора).
//
// ★ Исправления по ревизии 6–7 (16.09.2026, отчёт login_save_guard.md, раздел 8):
//   - С1: коммандлеты, удаляющие файлы через ФИЗИЧЕСКИЙ слой мимо обёртки (FPackageSourceControlHelper::Delete без контроля
//     версий: WorldPartitionConvert, ExternalActors, WorldPartitionBuilder, ConvertLevelsToExternalActors), при защищаемой
//     карте в аргументах (или «все карты»/коллекция/-DeleteSourceLevels) — отказ ЗАПУСКА: Error [LOGIN-GUARD] и немедленный
//     выход с кодом 1 до Commandlet->Main.
//   - М5: -ExecCmds с «EXECFILE …»/«exec …» (исполнители файлов команд) — автоматика.
//   - М3: при выходе процесса защита не снимается (Shutdown).
//
// ★ Общий список пакетов логина (16.09.2026, отчёт login_save_guard.md, раздел 9):
//   - Install читает Tools/data/asda_login_packages.json (AsdaLoginPackageList) — ТОТ ЖЕ файл, что Tools/asda_login_guard.py.
//     Ошибка чтения/разбора → fail-closed: в автоматической сессии защищаемым считается ВСЁ под /Game (строка Error).
//   - В автоматической сессии БЕЗ ASDA_ALLOW_LOGIN_WRITE=1 отказываются сохранение (IsPackageOKToSave) и удаление
//     (OnAssetsCanDelete) ПАКЕТОВ СЦЕНЫ ЛОГИНА из списка: BP HolyLight_SpotLight, Login_Shine, Shine_Dust, задник,
//     материалы, текстуры, окна UI/Login и т. д. Записи LOGIN_SHARED_EXCEPTIONS не защищаются.
//   - САМ УРОВЕНЬ (umap, _BuiltData, внешние акторы/объекты) по-прежнему открывает ТОЛЬКО ASDA_ALLOW_LOGIN_MAP_WRITE=1:
//     записи уровня из списка в набор «пакеты логина» не попадают.
//   - Файловый сторож (AsdaLoginFileGuard) держит оба набора: он закрывает пути мимо делегатов (rename_asset,
//     DeleteObjectsUnchecked, ConsolidateObjects), которыми ассеты логина стирались бы безнаказанно.
//   - Проверка: asda.LoginGuard.SelfTestPackageList (разбор JSON и сверка путей), asda.LoginGuard.DumpLoginPackages <файл>
//     (выгрузка множества пакетов логина по всему Content для сверки с Python).
//   - Стенд: ASDA_LOGIN_GUARD_TEST_PACKAGE принимает несколько записей через «;» с приставкой map:/asset:.
//
// ★ Правки по ревизии §9 (16.09.2026, отчёт login_save_guard.md, раздел 10):
//   - МЕЛК-1: пакет, чьё имя РАВНО пути папки OFPA (/Game/__ExternalActors__/Map/Login — файл
//     Content/__ExternalActors__/Map/Login.uasset), теперь держит НАБОР КАРТЫ: FindProtectedIn сверяет «имя + /», а
//     AsdaLoginFileGuard кладёт пути папок OFPA ещё и в основы файлов. Прежде такой пакет не защищал никто, хотя Python
//     (запись tree) считает его логином. Записи OFPA по-прежнему выбрасываются из набора «пакеты логина» (см. BuildAssetMatcher).
//   - МЕЛК-3: отказ запуска коммандлета-удалителя (С1а) проверяется ПОСЛЕ установки сторожа, а не до неё.
//
// ★ Сухой прогон пост-импорта UI (08.10.2026, ревизия poststeps_tools_harden_REVIEW.md, МЕЛК-1):
//   - Консольная переменная asda.PostSteps.DryRunSaveBlock (0/1). Пока 1 — IsPackageOKToSave отказывает ЛЮБОМУ пакету,
//     OnAssetsCanDelete — любому удалению (строки [DRYRUN-GUARD] в логе). Проверка стоит ДО сторожа Login и только добавляет
//     отказы. Ставит/снимает Tools/asda_ui_post_import.py (arm_dry_guard / disarm_dry_guard в finally).
//   - asda.PostSteps.DryRunSaveRefusals (только чтение) — счётчик отказов с начала процесса; Python берёт разность до/после.
#pragma once

#if WITH_EDITOR

// Установка/снятие обёртки делегата сохранения и подписки на запрет удаления. Вызывается модулем AsdaImporters
// (фаза PostEngineInit: к этому моменту UEditorEngine::Init уже привязал свой обработчик, а -ExecutePythonScript ещё не исполнялся).
namespace AsdaLoginSaveGuard
{
	// Прочитать командную строку и окружение, обернуть IsPackageOKToSaveDelegate, подписаться на OnAssetsCanDelete.
	void Install();

	// Вернуть прежний обработчик (если делегат всё ещё наш), снять подписку на удаление и файловую обёртку.
	// Только для выгрузки модуля во время работы (DLL освобождается, ModuleManager.cpp:1372-1379).
	void Uninstall();

	// ★ 16.09.2026 (ревизия М3): вызов из ShutdownModule. При выходе процесса (IsEngineExitRequested) НИЧЕГО не снимает:
	//   DLL не выгружается, а после ShutdownModule этого модуля ещё работают ShutdownModule других модулей (обратный порядок
	//   загрузки, ModuleManager.cpp:1438-1495) и поздние записи. При выгрузке модуля во время работы — Uninstall.
	void Shutdown();
}

#endif // WITH_EDITOR
