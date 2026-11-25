// Copyright (c) 2026 @sitxovski (Discord)
// AsdaLoginFileGuard — последний рубеж защиты уровня логина на уровне ФАЙЛОВОЙ СИСТЕМЫ UE (15.09.2026).
//
// ★ Зачем. AsdaLoginSaveGuard закрывает сохранение (IsPackageOKToSaveDelegate) и удаление (OnAssetsCanDelete), но
//   rename_asset/rename_directory без редиректора стирают старый пакет напрямую: AssetRenameManager.cpp:2036
//   → ObjectTools::CleanupAfterSuccessfulDelete → IFileManager::Delete, ни один делегат не спрашивается.
//   То же грозит любым будущим путям записи/удаления мимо делегатов.
//
// ★ Механизм UE 5.8. IFileManager (FFileManagerGeneric) при КАЖДОМ вызове берёт верхний слой
//   FPlatformFileManager::Get().GetPlatformFile() (FileManagerGeneric.h:38-42, «instead of caching the LowLevel»).
//   Обёртка IPlatformFile ставится сверху цепочки через SetPlatformFile — так же в рантайме делают PlatformDLC
//   (PlatformDLCModule.cpp:66-67) и FileSandbox (SandboxPlatformFile.cpp:164); снимается RemovePlatformFile,
//   как у PakFile (PakFileModule.cpp:28-35).
//   Отказ: DeleteFile/DeleteFiles, MoveFile (источник и цель), OpenWrite (оба варианта), OpenMappedEx2 с записью,
//   OpenMappedEx с AllowWrite (16.09: на Windows это GENERIC_WRITE, без OpenExisting — CREATE_ALWAYS),
//   CopyFile в цель, SetReadOnly(false), SetTimeStamp, DeleteDirectory, DeleteDirectoryRecursively (в том числе
//   для папок-родителей защищаемых файлов). IsReadOnly защищаемого файла отвечает «только чтение» (16.09: SaveWorld
//   проверяет это ДО DeleteExistingMapPackages, FileHelpers.cpp:1048 → :1091). Чтение и всё прочее — без проверок.
//
// ★ Физический слой (IPlatformFile::GetPlatformPhysical) обёртку НЕ проходит: FPackageSourceControlHelper::Delete без
//   контроля версий (PackageSourceControlHelper.cpp:64-71) удаляет файлы мимо цепочки. Коммандлеты-удалители с Login
//   в аргументах отказываются к запуску в AsdaLoginSaveGuard (см. отчёт login_save_guard.md, раздел 8).
//
// ★ Защищаемые файлы карты /Корень/Путь/Имя (сравнение нормализованных полных путей без учёта регистра):
//   <Content>/Путь/Имя.*, <Content>/Путь/Имя_BuiltData.*, <Content>/__ExternalActors__/Путь/Имя/**,
//   <Content>/__ExternalObjects__/Путь/Имя/**, а также (★ 16.09.2026, ревизия §9, МЕЛК-1)
//   <Content>/__ExternalActors__/Путь/Имя.* и <Content>/__ExternalObjects__/Путь/Имя.* — файл-пакет РЯДОМ с папкой OFPA,
//   чьё имя равно пути папки; правило «папка/**» его не ловило, а по общему списку это пакет логина.
//
// ★ 16.09.2026 (общий список): обёртка защищает ДВА набора с РАЗНЫМИ разрешениями:
//   - файлы УРОВНЕЙ (как раньше) — открывает только ASDA_ALLOW_LOGIN_MAP_WRITE=1;
//   - файлы ПАКЕТОВ СЦЕНЫ ЛОГИНА из общего списка Tools/data/asda_login_packages.json (AsdaLoginPackageList) — открывает
//     ASDA_ALLOW_LOGIN_WRITE=1. Записи самого уровня из списка в сторожевой набор не попадают (их держит набор карты),
//     поэтому при MAP=1 уровень пишется, а ассеты логина — нет, и наоборот.
//   Путь файла → путь пакета: <Content>/путь/Имя.расширение → /Game/путь/Имя; сверка — двоичным поиском по спискам
//   (O(глубина пути)), а не перебором 1529 пакетов. Быстрый разбор пути идёт без выделения памяти (TStringBuilder),
//   сложные формы (без диска, «D:хвост», «\\?\», 8.3, тома, UNC) уходят в полный разбор.
//
// ★ Ставится ТОЛЬКО из AsdaLoginSaveGuard::Install и только в автоматической сессии, где действует хотя бы один запрет.
//   В интерактивном редакторе владельца обёртки в цепочке нет вовсе.
//
// ★ Проверка: консоль asda.LoginGuard.SelfTestFilePaths (разбор путей, быстрый разбор против полного, пакеты логина),
//   asda.LoginGuard.FileProbe (прямые файловые операции над ТЕСТОВЫМИ пакетами ASDA_LOGIN_GUARD_TEST_PACKAGE, только при
//   установленной обёртке), asda.LoginGuard.FileBench [N] (замер файловых операций в Content/Temp/_LoginGuardBench —
//   ★ 16.09.2026, ревизия §9, СРЕДН-1: каталог перенесён из Saved внутрь Content, иначе горячий путь сверки с пакетами
//   логина не измерялся: путь в Saved выходил из MatchAssetPath на первом сравнении с <Content>).
#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Editor/AsdaLoginPackageList.h"

// Установка/снятие файловой обёртки. Вызывать на игровом потоке.
namespace AsdaLoginFileGuard
{
	// Что защищать и что уже разрешено (решение принимает AsdaLoginSaveGuard).
	struct FRules
	{
		TArray<FString> MapPackages;                                  // длинные имена защищаемых карт (уровень логина, стенд)
		const AsdaLoginPackageList::FMatcher* AssetMatcher = nullptr; // список пакетов логина (живёт до конца процесса) или nullptr
		bool bMapGuard = false;                                       // запрет на файлы уровней действует
		bool bAssetGuard = false;                                     // запрет на файлы пакетов логина действует
	};

	// Поставить обёртку сверху цепочки платформенных файлов. SessionReason — чем опознана автоматическая сессия,
	// MapPermissionHint/AssetPermissionHint — подсказки про разрешения для строк отказа.
	void Install(const FRules& Rules, const FString& SessionReason, const FString& MapPermissionHint, const FString& AssetPermissionHint);

	// Снять обёртку с цепочки. Объект намеренно не удаляется: фоновый поток мог взять указатель на верхний слой до снятия.
	// ★ 16.09.2026: вызывается ТОЛЬКО при выгрузке модуля во время работы (DLL освобождается). При выходе процесса
	//   обёртка остаётся в цепочке до конца (DLL не выгружается, ModuleManager.cpp:1372-1379) — см. AsdaLoginSaveGuard::Shutdown.
	void Uninstall();

	// Стоит ли обёртка в цепочке сейчас.
	bool IsInstalled();

	// Стендовый пакет из ASDA_LOGIN_GUARD_TEST_PACKAGE.
	struct FTestPackage
	{
		FString Package;   // длинное имя
		bool bMap = true;  // true — защищается как УРОВЕНЬ (ASDA_ALLOW_LOGIN_MAP_WRITE), false — как пакет логина (ASDA_ALLOW_LOGIN_WRITE)
	};

	// Разобрать ASDA_LOGIN_GUARD_TEST_PACKAGE: несколько записей через «;», каждая — «пакет», «map:пакет» или «asset:пакет».
	// Без приставки вид определяется по диску: есть файл .umap → уровень, иначе — пакет логина.
	void ParseTestPackages(const FString& Value, TArray<FTestPackage>& Out);

	// Попадает ли путь (любая форма Win32/NT: относительный, «/…» без диска, «D:…» относительно папки диска, «\\?\», «\??\»,
	// «\\localhost\D$\», «\\?\Volume{…}\», короткие имена 8.3) в файлы защищаемых карт. bAncestors — считать ли папки-родители.
	// OutMap — длинное имя карты при совпадении. Для проверки аргументов коммандлетов при запуске (AsdaLoginSaveGuard).
	bool PathTargetsMaps(const TArray<FString>& MapPackageNames, const FString& Path, bool bAncestors, FString& OutMap);

	// ★ 16.09.2026 (общий список). Обойти ВСЕ пакеты (.uasset/.umap) в Content и сверить их файловыми правилами —
	// для дампа множества «пакетов логина» и сверки с Python (AsdaLoginSaveGuard: asda.LoginGuard.DumpLoginPackages).
	// Visitor(путь пакета «/Game/…», защищён ли файл правилами, совпал ли быстрый разбор пути с полным на трёх формах пути).
	void ForEachContentPackage(const FRules& Rules, TFunctionRef<void(const FString& PackagePath, bool bFileProtected, bool bFastMatchesSlow)> Visitor);
}

#endif // WITH_EDITOR
