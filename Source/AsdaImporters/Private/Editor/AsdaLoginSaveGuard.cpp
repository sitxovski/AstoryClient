// Copyright (c) 2026 @sitxovski (Discord)
// AsdaLoginSaveGuard — отказ движка СОХРАНЯТЬ и УДАЛЯТЬ пакеты уровня логина в АВТОМАТИЧЕСКОЙ сессии (см. заголовок).
#include "Editor/AsdaLoginSaveGuard.h"

#if WITH_EDITOR

#include "Editor/AsdaLoginFileGuard.h"
#include "Editor/AsdaLoginPackageList.h"

#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "Misc/PackagePath.h"
#include "Misc/OutputDevice.h"
#include "HAL/PlatformMisc.h"
#include "HAL/IConsoleManager.h"
#include "ParseExecCommands.h"
#include "Editor.h"
#include "CoreGlobals.h"
#include "Commandlets/Commandlet.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "CollectionManagerModule.h"
#include "CollectionManagerTypes.h"
#include "ICollectionManager.h"
#include "ICollectionContainer.h"

// Своя категория: строки [LOGIN-GUARD] ищутся в логе прогонов.
DEFINE_LOG_CATEGORY_STATIC(LogAsdaLoginGuard, Log, All);

namespace AsdaLoginSaveGuard
{
	namespace
	{
		// Защищаемый пакет-уровень владельца.
		const TCHAR* const LoginPackage = TEXT("/Game/Map/Login");

		// ★ 15.09.2026 (ревизия): переменная, которая ОДНА открывает запись/удаление УРОВНЯ логина
		//   (umap, _BuiltData, внешние акторы/объекты).
		const TCHAR* const MapWriteEnv = TEXT("ASDA_ALLOW_LOGIN_MAP_WRITE");

		// Переменная ассетов сцены логина (Tools/asda_login_guard.py). Уровень она больше НЕ открывает — здесь только для лога.
		const TCHAR* const AssetWriteEnv = TEXT("ASDA_ALLOW_LOGIN_WRITE");

		// Одна защищаемая «карта»: сам пакет, его _BuiltData и папки внешних акторов/объектов (OFPA).
		struct FProtectedMap
		{
			FString Package;            // /Game/Map/Login
			FString BuiltData;          // /Game/Map/Login_BuiltData
			FString ExternalActorsRoot; // /Game/__ExternalActors__/Map/Login/
			FString ExternalObjectsRoot;// /Game/__ExternalObjects__/Map/Login/
		};

		// Защищаемые карты (Login + стендовый пакет из ASDA_LOGIN_GUARD_TEST_PACKAGE).
		TArray<FProtectedMap> ProtectedMaps;

		// Сессия автоматическая (коммандлет/unattended/скрипт/без экрана) — вычисляется один раз при установке.
		bool bAutomaticSession = false;

		// Чем опознана автоматическая сессия (для лога).
		FString AutomaticReason;

		// ASDA_ALLOW_LOGIN_MAP_WRITE=1 на момент СТАРТА процесса. Читается один раз при установке, чтобы Python-скрипт
		// не мог выдать разрешение сам себе через os.environ посреди прогона.
		bool bMapWriteAllowedByEnv = false;

		// ASDA_ALLOW_LOGIN_WRITE=1 на момент старта: открывает ПАКЕТЫ сцены логина (уровень — нет).
		bool bAssetWriteEnvSet = false;

		// ★ 16.09.2026 (общий список). Пакеты сцены логина из Tools/data/asda_login_packages.json. Объект в КУЧЕ и намеренно
		//   НЕ освобождается: на него ссылается файловая обёртка, которая тоже живёт до конца процесса (см. М5) — при выгрузке
		//   DLL во время работы обёртка снимается с цепочки, но фоновый поток мог взять её указатель раньше.
		const AsdaLoginPackageList::FMatcher* AssetMatcher = nullptr;

		// Строка состояния списка (для лога установки).
		FString ListStatus;

		// Прежний обработчик делегата (UEditorEngine::IsPackageOKToSave).
		FCoreUObjectDelegates::FIsPackageOKToSaveDelegate PreviousDelegate;

		// Ручка нашей привязки — снимать обёртку, только если делегат всё ещё наш.
		FDelegateHandle OurHandle;

		// Ручка подписки на FEditorDelegates::OnAssetsCanDelete (запрет удаления).
		FDelegateHandle CanDeleteHandle;

		// Установлена ли обёртка.
		bool bInstalled = false;

		// ★ 08.10.2026 (ревизия `poststeps_tools_harden_REVIEW.md`, МЕЛК-1): ЗАПРЕТ ЗАПИСИ НА ВРЕМЯ СУХОГО ПРОГОНА
		//   пост-импорта UI. Python-прокси (`asda_ui_post_import.arm_dry_guard`) обходится 8 способами (атрибут экземпляра,
		//   functools.partial, CDO, __subclasses__ …), последняя сетка (снимок Content) ловит их только ПОСЛЕ записи.
		//   Здесь — отказ на уровне движка: пока флаг ≠ 0, IsPackageOKToSave отказывает ЛЮБОМУ пакету (SavePackage2.cpp:218
		//   не пишет файл), OnAssetsCanDelete — любому удалению. Флаг ставит/снимает Python консольной командой
		//   «asda.PostSteps.DryRunSaveBlock 1/0» (`arm_dry_guard` / `disarm_dry_guard` в finally). Сторож Login не меняется:
		//   проверка сухого режима идёт ДО него и только добавляет отказы.
		int32 DryRunSaveBlock = 0;

		// Счётчик отказов сухого режима с начала процесса (только чтение из консоли; Python берёт разность до/после).
		int32 DryRunSaveRefusals = 0;

		// Консольная переменная флага сухого режима (ставится из Python: unreal.SystemLibrary.execute_console_command).
		FAutoConsoleVariableRef CVarDryRunSaveBlock(
			TEXT("asda.PostSteps.DryRunSaveBlock"),
			DryRunSaveBlock,
			TEXT("1 — сухой прогон пост-импорта UI: движок отказывает в сохранении и удалении ЛЮБОГО пакета (asda_ui_post_import.arm_dry_guard); 0 — обычный режим."),
			ECVF_Default);

		// Консольная переменная-счётчик отказов сухого режима (только чтение из консоли). ★ Не Ref-переменная: у
		//   FConsoleVariableRef GetInt() отдаёт копию MainValue, прямое изменение int32 он не видит (ConsoleManager.cpp:1786,
		//   проверено прогоном 08.10.2026: счётчик читался 0) — значение выставляется Set() при каждом отказе.
		TAutoConsoleVariable<int32> CVarDryRunSaveRefusals(
			TEXT("asda.PostSteps.DryRunSaveRefusals"),
			0,
			TEXT("Число отказов сохранения/удаления под asda.PostSteps.DryRunSaveBlock с начала процесса (только чтение)."),
			ECVF_ReadOnly);

		// Учесть отказ сухого режима: счётчик + его консольная переменная.
		void CountDryRunRefusal()
		{
			++DryRunSaveRefusals;
			CVarDryRunSaveRefusals.AsVariable()->Set(DryRunSaveRefusals, ECVF_SetByCode);
		}

		// Значение переменной окружения ровно «1» (пробелы по краям не в счёт).
		bool IsEnvOne(const TCHAR* Name)
		{
			return FPlatformMisc::GetEnvironmentVariable(Name).TrimStartAndEnd() == TEXT("1");
		}

		// Собрать описание защищаемой карты по длинному имени пакета «/Корень/путь/Имя».
		bool MakeProtectedMap(const FString& InPackageName, FProtectedMap& Out)
		{
			FString Name = InPackageName.TrimStartAndEnd();
			if (!FPackageName::IsValidLongPackageName(Name))
			{
				return false;
			}
			// «/Game/Map/Login» → корень «/Game», относительный путь «Map/Login».
			int32 SecondSlash = INDEX_NONE;
			if (!Name.RightChop(1).FindChar(TEXT('/'), SecondSlash))
			{
				return false;
			}
			SecondSlash += 1;
			const FString Root = Name.Left(SecondSlash);          // /Game
			const FString Relative = Name.RightChop(SecondSlash + 1); // Map/Login
			Out.Package = Name;
			Out.BuiltData = Name + TEXT("_BuiltData");
			Out.ExternalActorsRoot = Root + TEXT("/") + FPackagePath::GetExternalActorsFolderName() + TEXT("/") + Relative + TEXT("/");
			Out.ExternalObjectsRoot = Root + TEXT("/") + FPackagePath::GetExternalObjectsFolderName() + TEXT("/") + Relative + TEXT("/");
			return true;
		}

		// Какой карте из НАБОРА принадлежит имя пакета (nullptr — ничьё).
		// ★ 16.09.2026 (ревизия §9, МЕЛК-1): сверка идёт по «имя + /», поэтому под карту попадает и пакет, чьё имя РАВНО пути
		//   папки OFPA (/Game/__ExternalActors__/Map/Login — файл Content/__ExternalActors__/Map/Login.uasset). БЫЛО:
		//   StartsWith(«…/Login/») требовал хвостовой «/», такой пакет не попадал ни в набор карты, ни в набор пакетов логина
		//   (в BuildAssetMatcher эти две записи выбрасываются), а Python (tree) считает его пакетом логина — расхождение множеств.
		// ★ ТРИ МЕСТА, где ловится путь папки OFPA, правятся ВМЕСТЕ (ревизия §10.7): 1) здесь — набор карты;
		//   2) AsdaLoginFileGuard::MakeProtectedFiles — Out.FileStems (файл РЯДОМ с папкой); 3) там же Out.Dirs («папка/**»).
		const FProtectedMap* FindProtectedIn(const TArray<FProtectedMap>& Maps, const FString& PackageName)
		{
			const FString WithSlash = PackageName + TEXT("/");
			for (const FProtectedMap& Map : Maps)
			{
				if (PackageName.Equals(Map.Package, ESearchCase::IgnoreCase)
					|| PackageName.Equals(Map.BuiltData, ESearchCase::IgnoreCase)
					|| WithSlash.StartsWith(Map.ExternalActorsRoot, ESearchCase::IgnoreCase)
					|| WithSlash.StartsWith(Map.ExternalObjectsRoot, ESearchCase::IgnoreCase))
				{
					return &Map;
				}
			}
			return nullptr;
		}

		// То же по действующему набору установки.
		const FProtectedMap* FindProtected(const FString& PackageName)
		{
			return FindProtectedIn(ProtectedMaps, PackageName);
		}

		// Первое слово консольной команды = Word (без учёта регистра), а за ним конец строки или НЕ буква/цифра/«_»
		// (пробел, «.», кавычка). «py x.py», «py.x», «PY "x.py"» — да; «pyramid», «py_x» — нет.
		bool CommandStartsWithWord(const FString& Command, const TCHAR* Word)
		{
			const FString Trimmed = Command.TrimStartAndEnd();
			const int32 WordLen = FCString::Strlen(Word);
			if (Trimmed.Len() < WordLen || FCString::Strnicmp(*Trimmed, Word, WordLen) != 0)
			{
				return false;
			}
			if (Trimmed.Len() == WordLen)
			{
				return true;
			}
			const TCHAR Next = Trimmed[WordLen];
			return !FChar::IsAlnum(Next) && Next != TEXT('_');
		}

		// Признаки автоматики В КОМАНДНОЙ СТРОКЕ (без состояния движка — его добавляет DetectAutomaticSession).
		// Отдельная функция, чтобы разбор проверялся самотестом на наборе строк (asda.LoginGuard.SelfTestCmdLine).
		void CollectCommandLineReasons(const TCHAR* Cmd, TArray<FString>& Reasons)
		{
			// Та же проверка, что у PythonScriptPlugin (EditorPythonExecuter.cpp:144): Strifind «-ExecutePythonScript».
			if (FCString::Strifind(Cmd, TEXT("-ExecutePythonScript"), true))
			{
				Reasons.Add(TEXT("-ExecutePythonScript"));
			}
			FString RunValue;
			if (FParse::Value(Cmd, TEXT("-run="), RunValue) && RunValue.Equals(TEXT("pythonscript"), ESearchCase::IgnoreCase))
			{
				Reasons.Add(TEXT("-run=pythonscript"));
			}
			if (FParse::Param(Cmd, TEXT("RenderOffscreen")))
			{
				Reasons.Add(TEXT("-RenderOffscreen"));
			}
			if (FParse::Param(Cmd, TEXT("nullrhi")))
			{
				Reasons.Add(TEXT("-nullrhi"));
			}
			if (FParse::Param(Cmd, TEXT("AutomatedTests")))
			{
				Reasons.Add(TEXT("-AutomatedTests"));
			}
			// Файл консольных команд — тот же ключ и разбор, что у движка (UnrealEngine.cpp:2546, FParse::Value «EXEC=»).
			FString ExecFile;
			if (FParse::Value(Cmd, TEXT("EXEC="), ExecFile))
			{
				Reasons.Add(FString::Printf(TEXT("-EXEC=%s"), *ExecFile));
			}
			// -ExecCmds: тот же разбор, что у движка (ParseExecCommands.cpp:57-69 — все вхождения ключа, значение
			// в кавычках целиком; ParseExecCmds :11 — разделитель «,», запятые внутри '…' не делят).
			// ★ Автоматикой считаются ТОЛЬКО Python-команды и «Automation …»: «stat fps» и прочие команды владельца — нет.
			const TCHAR* Stream = Cmd;
			FString Line;
			while (Stream && FParse::Value(Stream, TEXT("ExecCmds="), Line, /*bShouldStopOnSeparator*/false, &Stream))
			{
				for (const FString& Command : ParseExecCommands::ParseExecCmds(Line))
				{
					if (CommandStartsWithWord(Command, TEXT("py")))
					{
						Reasons.AddUnique(TEXT("-ExecCmds: py"));
					}
					else if (CommandStartsWithWord(Command, TEXT("Automation")))
					{
						Reasons.AddUnique(TEXT("-ExecCmds: Automation"));
					}
					// ★ 16.09.2026 (ревизия М5): исполнители ФАЙЛОВ команд — «EXECFILE <файл>» (UEditorEngine::SafeExec,
					//   EditorServer.cpp:329-338 → ExecFile :5635, построчно Exec) и «exec <файл>» (ULocalPlayer::Exec,
					//   LocalPlayer.cpp:1580 → HandleExecCommand :1458 → ExecMacro; тот же вид, что даёт -EXEC=, UnrealEngine.cpp:2546).
					else if (CommandStartsWithWord(Command, TEXT("EXECFILE")))
					{
						Reasons.AddUnique(TEXT("-ExecCmds: EXECFILE"));
					}
					else if (CommandStartsWithWord(Command, TEXT("exec")))
					{
						Reasons.AddUnique(TEXT("-ExecCmds: exec"));
					}
				}
			}
		}

		// Опознать автоматическую сессию по состоянию движка и командной строке.
		void DetectAutomaticSession()
		{
			TArray<FString> Reasons;
			if (IsRunningCommandlet())
			{
				Reasons.Add(TEXT("commandlet"));
			}
			if (FApp::IsUnattended())
			{
				Reasons.Add(TEXT("unattended"));
			}
			CollectCommandLineReasons(FCommandLine::Get(), Reasons);
			bAutomaticSession = Reasons.Num() > 0;
			AutomaticReason = FString::Join(Reasons, TEXT(", "));
		}

		// Подсказка в отказе по УРОВНЮ: чем он открывается и почему ASDA_ALLOW_LOGIN_WRITE не помог.
		FString PermissionHint()
		{
			return bAssetWriteEnvSet
				? FString::Printf(TEXT("разрешение — только %s=1 при запуске; %s=1 задан, но уровень он не открывает"), MapWriteEnv, AssetWriteEnv)
				: FString::Printf(TEXT("разрешение — только %s=1 при запуске"), MapWriteEnv);
		}

		// Подсказка в отказе по ПАКЕТАМ СЦЕНЫ ЛОГИНА (16.09.2026, общий список).
		FString AssetPermissionHint()
		{
			return FString::Printf(TEXT("разрешение — только %s=1 при запуске (список %s)"), AssetWriteEnv, *ListStatus);
		}

		// Запрет УРОВНЯ действует: сессия автоматическая и уровень не разрешён.
		bool IsGuardActive()
		{
			return bAutomaticSession && !bMapWriteAllowedByEnv;
		}

		// Запрет ПАКЕТОВ СЦЕНЫ ЛОГИНА действует: сессия автоматическая, список подключён и ассеты не разрешены.
		bool IsAssetGuardActive()
		{
			return bAutomaticSession && !bAssetWriteEnvSet && AssetMatcher != nullptr;
		}

		// Вид защиты пакета.
		enum class EProtectKind : uint8
		{
			None,  // не защищён
			Map,   // уровень (ASDA_ALLOW_LOGIN_MAP_WRITE)
			Asset, // пакет сцены логина из общего списка (ASDA_ALLOW_LOGIN_WRITE)
		};

		// Что защищает этот пакет в ЗАДАННЫХ наборах (Maps — карты, Matcher — список пакетов логина; nullptr — списка нет).
		// Уровень проверяется первым: записи уровня в набор «пакеты логина» не попадают.
		EProtectKind ClassifyPackageIn(const TArray<FProtectedMap>& Maps, const AsdaLoginPackageList::FMatcher* Matcher,
			const FString& PackageName, FString& OutWho)
		{
			if (const FProtectedMap* Hit = FindProtectedIn(Maps, PackageName))
			{
				OutWho = FString::Printf(TEXT("уровень %s"), *Hit->Package);
				return EProtectKind::Map;
			}
			if (Matcher && Matcher->Classify(PackageName) == AsdaLoginPackageList::EOwnership::Owned)
			{
				OutWho = FString::Printf(TEXT("пакет сцены логина %s"), *PackageName);
				return EProtectKind::Asset;
			}
			return EProtectKind::None;
		}

		// То же по действующим наборам установки.
		EProtectKind ClassifyPackage(const FString& PackageName, FString& OutWho)
		{
			return ClassifyPackageIn(ProtectedMaps, AssetMatcher, PackageName, OutWho);
		}

		// Действует ли запрет для этого вида.
		bool IsRefused(EProtectKind Kind)
		{
			return (Kind == EProtectKind::Map && IsGuardActive()) || (Kind == EProtectKind::Asset && IsAssetGuardActive());
		}

		// Подсказка про разрешение для вида защиты.
		FString HintFor(EProtectKind Kind)
		{
			return Kind == EProtectKind::Map ? PermissionHint() : AssetPermissionHint();
		}

		// Обёртка IsPackageOKToSaveDelegate: наш отказ, иначе — решение прежнего обработчика.
		bool IsPackageOKToSave(UPackage* InPackage, const FString& InFilename, FOutputDevice* Error)
		{
			// ★ 08.10.2026 (МЕЛК-1): сухой прогон пост-импорта — отказ ЛЮБОМУ пакету, до сторожа Login.
			if (DryRunSaveBlock != 0)
			{
				CountDryRunRefusal();
				const FString PackageName = InPackage ? InPackage->GetName() : FString(TEXT("<null>"));
				UE_LOG(LogAsdaLoginGuard, Error, TEXT("[DRYRUN-GUARD] отказ сохранения: сухой прогон пост-импорта (asda.PostSteps.DryRunSaveBlock=1), пакет %s, файл %s"),
					*PackageName, *InFilename);
				if (Error)
				{
					Error->Logf(ELogVerbosity::Warning, TEXT("[DRYRUN-GUARD] отказ сохранения %s: сухой прогон"), *PackageName);
				}
				return false;
			}
			if (bAutomaticSession)
			{
				// Проверяем и имя пакета, и целевой файл: пакет с другим именем, пишущий в файл Login.umap, тоже отказ.
				FString Who;
				EProtectKind Kind = InPackage ? ClassifyPackage(InPackage->GetName(), Who) : EProtectKind::None;
				if (!IsRefused(Kind) && !InFilename.IsEmpty())
				{
					FString FilePackageName;
					if (FPackageName::TryConvertFilenameToLongPackageName(InFilename, FilePackageName))
					{
						FString FileWho;
						const EProtectKind FileKind = ClassifyPackage(FilePackageName, FileWho);
						if (IsRefused(FileKind))
						{
							Kind = FileKind;
							Who = FileWho;
						}
					}
				}
				if (IsRefused(Kind))
				{
					const FString PackageName = InPackage ? InPackage->GetName() : FString(TEXT("<null>"));
					UE_LOG(LogAsdaLoginGuard, Error, TEXT("[LOGIN-GUARD] отказ сохранения: %s в автоматической сессии (пакет %s, файл %s; сессия: %s; %s)"),
						*Who, *PackageName, *InFilename, *AutomaticReason, *HintFor(Kind));
					if (Error)
					{
						Error->Logf(ELogVerbosity::Warning, TEXT("[LOGIN-GUARD] отказ сохранения: %s в автоматической сессии"), *Who);
					}
					return false;
				}
			}
			if (PreviousDelegate.IsBound())
			{
				return PreviousDelegate.Execute(InPackage, InFilename, Error);
			}
			return true;
		}

		// ★ 15.09.2026 (ревизия, КРИТ): удаление идёт мимо делегата сохранения —
		//   EditorAssetLibrary.delete_asset/delete_directory → ObjectTools::ForceDeleteObjects (ObjectTools.cpp:3621)
		//   → CleanupAfterSuccessfulDelete (:2503) → IFileManager::Delete (:2672/:2709). Перед удалением движок спрашивает
		//   FEditorDelegates::OnAssetsCanDelete: ForceDeleteObjects :3630, DeleteItems/DeleteObjects :3116, DeleteSingleObject :3459.
		//   Список уже расширен AddExtraObjectsToDelete (внешние пакеты уровня добавлены как UPackage).
		void OnAssetsCanDelete(const TArray<UObject*>& InObjectsToDelete, FCanDeleteAssetResult& OutCanDelete)
		{
			// ★ 08.10.2026 (МЕЛК-1): сухой прогон пост-импорта — отказ любому удалению.
			if (DryRunSaveBlock != 0 && InObjectsToDelete.Num() > 0)
			{
				CountDryRunRefusal();
				const UObject* First = InObjectsToDelete[0];
				UE_LOG(LogAsdaLoginGuard, Error, TEXT("[DRYRUN-GUARD] отказ удаления: сухой прогон пост-импорта (asda.PostSteps.DryRunSaveBlock=1), объектов %d, первый %s"),
					InObjectsToDelete.Num(), First ? *First->GetPathName() : TEXT("<null>"));
				OutCanDelete.Set(false);
				return;
			}
			if (!IsGuardActive() && !IsAssetGuardActive())
			{
				return;
			}
			bool bRefused = false;
			for (const UObject* Object : InObjectsToDelete)
			{
				if (!Object)
				{
					continue;
				}
				// Цепочка внешних: GetExternalPackage() возвращает сам пакет у UPackage и внешний пакет у OFPA-актора,
				// у объекта внутри уровня — пакет уровня через Outer. Любое звено защищаемое → отказ.
				EProtectKind Kind = EProtectKind::None;
				FString Who;
				FString HitPackage;
				for (const UObject* It = Object; It && !IsRefused(Kind); It = It->GetOuter())
				{
					if (const UPackage* ItPackage = It->GetExternalPackage())
					{
						const FString Candidate = ItPackage->GetName();
						FString CandidateWho;
						const EProtectKind CandidateKind = ClassifyPackage(Candidate, CandidateWho);
						if (IsRefused(CandidateKind))
						{
							Kind = CandidateKind;
							Who = CandidateWho;
							HitPackage = Candidate;
						}
					}
				}
				if (IsRefused(Kind))
				{
					bRefused = true;
					UE_LOG(LogAsdaLoginGuard, Error, TEXT("[LOGIN-GUARD] отказ удаления: %s в автоматической сессии (объект %s, пакет %s; сессия: %s; %s)"),
						*Who, *Object->GetPathName(), *HitPackage, *AutomaticReason, *HintFor(Kind));
				}
			}
			if (bRefused)
			{
				OutCanDelete.Set(false);
			}
		}

		// ★ 16.09.2026 (ревизия С1): коммандлеты движка, удаляющие файлы пакетов через FPackageSourceControlHelper::Delete.
		//   Без контроля версий это ФИЗИЧЕСКИЙ слой (PackageSourceControlHelper.cpp:64-71: GetPlatformPhysical().SetReadOnly(false)
		//   + DeleteFile) — файловая обёртка его не видит. Имена классов без суффикса «Commandlet» (LaunchEngineLoop.cpp:2330-2337
		//   дописывает его к «-run=»).
		struct FPhysicalDeleteCommandlet
		{
			const TCHAR* Name;  // имя класса без «Commandlet»
			const TCHAR* Where; // где удаляет (для строки отказа)
		};
		const FPhysicalDeleteCommandlet PhysicalDeleteCommandlets[] =
		{
			{ TEXT("WorldPartitionConvert"), TEXT("WorldPartitionConvertCommandlet.cpp:822/:830 — прежние результаты, :1530 — исходные уровни при -DeleteSourceLevels") },
			{ TEXT("ExternalActors"), TEXT("ExternalActorsCommandlet.cpp:484") },
			{ TEXT("WorldPartitionBuilder"), TEXT("билдеры через WorldPartitionBuilderHelpers.cpp:140-177 и WorldPartitionBuilder.cpp:470") },
			{ TEXT("ConvertLevelsToExternalActors"), TEXT("ConvertLevelsToExternalActorsCommandlet.cpp:248 при -repair (:146)") },
		};

		// Имя класса коммандлета без пути «/Script/Модуль.» и суффикса «Commandlet».
		FString CommandletBaseName(FString Name)
		{
			Name.TrimStartAndEndInline();
			Name.TrimQuotesInline();
			int32 Dot = INDEX_NONE;
			if (Name.FindLastChar(TEXT('.'), Dot))
			{
				Name.RightChopInline(Dot + 1);
			}
			if (Name.EndsWith(TEXT("Commandlet"), ESearchCase::IgnoreCase))
			{
				Name.LeftChopInline(10);
			}
			return Name;
		}

		// Коммандлет из списка в командной строке: «-run=Имя[Commandlet]» (LaunchEngineLoop.cpp:2330), не-ключ «…Commandlet»
		// (:2320), первый не-ключ, к которому движок допишет «Commandlet» (:2975-2999). OutToken — как записан в строке.
		const FPhysicalDeleteCommandlet* FindPhysicalDeleteCommandlet(const TArray<FString>& Tokens, const TArray<FString>& Switches, FString& OutToken)
		{
			// ★ 16.09.2026 (ревизия М1): порядок движка, а не «все кандидаты подряд». LaunchEngineLoop.cpp:2318-2325 — ПЕРВЫЙ
			//   не-ключ, оканчивающийся на «Commandlet»; если такого нет — первый ключ «-run=» (:2327-2341, суффикс дописывается);
			//   и только если нет ни того, ни другого (IsModeSelected == false) — первый не-ключ (:2975-2999).
			//   БЫЛО: первый не-ключ проверялся ВСЕГДА, поэтому «-run=pythonscript … ExternalActors» давало ложный отказ
			//   запуска по аргументу, который движок коммандлетом не считает.
			FString Candidate;
			for (const FString& Token : Tokens)
			{
				if (Token.TrimQuotes().EndsWith(TEXT("Commandlet"), ESearchCase::IgnoreCase))
				{
					Candidate = Token;
					break;
				}
			}
			if (Candidate.IsEmpty())
			{
				for (const FString& Switch : Switches)
				{
					if (Switch.StartsWith(TEXT("run="), ESearchCase::IgnoreCase))
					{
						Candidate = Switch;
						break;
					}
				}
			}
			if (Candidate.IsEmpty())
			{
				for (const FString& Token : Tokens)
				{
					if (!Token.TrimQuotes().EndsWith(TEXT(".uproject"), ESearchCase::IgnoreCase))
					{
						Candidate = Token;
						break;
					}
				}
			}
			if (Candidate.IsEmpty())
			{
				return nullptr;
			}
			const FString Base = CommandletBaseName(Candidate.StartsWith(TEXT("run="), ESearchCase::IgnoreCase) ? Candidate.RightChop(4) : Candidate);
			for (const FPhysicalDeleteCommandlet& Entry : PhysicalDeleteCommandlets)
			{
				if (Base.Equals(Entry.Name, ESearchCase::IgnoreCase))
				{
					OutToken = Candidate;
					return &Entry;
				}
			}
			return nullptr;
		}

		// Существует ли коллекция с таким именем — тот же разбор, что у WorldPartitionBuilderCommandlet.cpp:92-93.
		bool IsExistingCollection(const FString& Name)
		{
			ICollectionManager& Manager = FCollectionManagerModule::GetModule().Get();
			TSharedPtr<ICollectionContainer> Container;
			FName CollectionName;
			ECollectionShareType::Type ShareType = ECollectionShareType::CST_All;
			return Manager.TryParseCollectionPath(Name, &Container, &CollectionName, &ShareType) && Container.IsValid()
				&& Container->CollectionExists(CollectionName, ShareType);
		}

		// Указывает ли аргумент коммандлета на защищаемую карту. Аргумент может быть списком через «,» (WorldPartitionBuilder,
		// :130). Каждый элемент:
		//   «*…» — все карты проекта (:102-125);
		//   короткое имя («Login», «Login.umap») — FPackageName::SearchForPackageOnDisk ищет его по всем корням контента
		//     (PackageName.cpp:2661-2694): совпадение с коротким именем карты или её _BuiltData; bCollections — ещё и коллекция
		//     (набор карт при запуске не проверяется — отказ);
		//   «/Корень/…» — пакет, путь объекта («/Game/Map/Login.Login») или папка (билдер обходит папки рекурсивно, :137-150):
		//     сама карта, _BuiltData, внутри папок OFPA или папка-родитель любого из них;
		//   любой путь файла/папки (все формы, AsdaLoginFileGuard::PathTargetsMaps) — файлы карты или их родитель.
		// ★ 16.09.2026 (ревизия М2): bSwitchValue — это ЗНАЧЕНИЕ КЛЮЧА «-Ключ=Значение», а не аргумент-карта. У значений
		//   ключей отбрасываются два источника ложных отказов: короткое имя с чужим расширением («-log=Login.txt») и
		//   файловый путь-РОДИТЕЛЬ («-ProjectDir=<папка проекта>»). Сам файл карты в значении ключа отказ по-прежнему даёт.
		bool ItemTargetsProtected(const FString& InItem, const TArray<FProtectedMap>& Maps, bool bCollections, bool bSwitchValue, FString& OutWhy)
		{
			TArray<FString> MapNames;
			for (const FProtectedMap& Map : Maps)
			{
				MapNames.Add(Map.Package);
			}
			TArray<FString> Pieces;
			InItem.TrimStartAndEnd().TrimQuotes().ParseIntoArray(Pieces, TEXT(","), /*CullEmpty*/ true);
			for (FString Piece : Pieces)
			{
				Piece.TrimStartAndEndInline();
				Piece.TrimQuotesInline();
				if (Piece.IsEmpty())
				{
					continue;
				}
				if (Piece.StartsWith(TEXT("*")))
				{
					OutWhy = FString::Printf(TEXT("«%s» — все карты проекта"), *Piece);
					return true;
				}
				const bool bFileLike = Piece.Contains(TEXT("\\")) || (Piece.Len() >= 2 && Piece[1] == TEXT(':'))
					|| Piece.StartsWith(TEXT(".")) || Piece.StartsWith(TEXT("//"));
				if (!bFileLike && !Piece.Contains(TEXT("/")))
				{
					const FString Short = FPaths::GetBaseFilename(Piece);
					// У токена-аргумента расширение не мешает: SearchForPackageOnDisk ищет «Имя.*» (PackageName.cpp:2676-2686).
					// У значения ключа чужое расширение означает, что это не карта («-log=Login.txt»).
					const FString Extension = FPaths::GetExtension(Piece);
					if (bSwitchValue && !Extension.IsEmpty()
						&& !Extension.Equals(TEXT("umap"), ESearchCase::IgnoreCase) && !Extension.Equals(TEXT("uasset"), ESearchCase::IgnoreCase))
					{
						continue;
					}
					for (const FProtectedMap& Map : Maps)
					{
						if (Short.Equals(FPackageName::GetShortName(Map.Package), ESearchCase::IgnoreCase)
							|| Short.Equals(FPackageName::GetShortName(Map.BuiltData), ESearchCase::IgnoreCase))
						{
							OutWhy = FString::Printf(TEXT("короткое имя «%s» — %s"), *Piece, *Map.Package);
							return true;
						}
					}
					if (bCollections && IsExistingCollection(Piece))
					{
						OutWhy = FString::Printf(TEXT("коллекция «%s» — набор карт при запуске не проверяется"), *Piece);
						return true;
					}
					continue;
				}
				if (!bFileLike && Piece.StartsWith(TEXT("/")))
				{
					FString Package = Piece;
					int32 LastSlash = 0;
					Package.FindLastChar(TEXT('/'), LastSlash);
					const int32 Dot = Package.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromStart, LastSlash);
					if (Dot != INDEX_NONE)
					{
						Package.LeftInline(Dot); // путь объекта «/Game/Map/Login.Login[:Подобъект]» → пакет
					}
					while (Package.EndsWith(TEXT("/")))
					{
						Package.LeftChopInline(1);
					}
					const FString Folder = Package + TEXT("/");
					for (const FProtectedMap& Map : Maps)
					{
						if (Package.IsEmpty()
							|| Package.Equals(Map.Package, ESearchCase::IgnoreCase) || Package.Equals(Map.BuiltData, ESearchCase::IgnoreCase)
							|| Folder.StartsWith(Map.ExternalActorsRoot, ESearchCase::IgnoreCase) || Folder.StartsWith(Map.ExternalObjectsRoot, ESearchCase::IgnoreCase)
							|| Map.Package.StartsWith(Folder, ESearchCase::IgnoreCase)
							|| Map.ExternalActorsRoot.StartsWith(Folder, ESearchCase::IgnoreCase) || Map.ExternalObjectsRoot.StartsWith(Folder, ESearchCase::IgnoreCase))
						{
							OutWhy = FString::Printf(TEXT("пакет/папка «%s» — %s"), *Piece, *Map.Package);
							return true;
						}
					}
				}
				FString HitMap;
				// У значения ключа папки-родители не в счёт (иначе «-ProjectDir=<папка проекта>» — всегда отказ).
				if (AsdaLoginFileGuard::PathTargetsMaps(MapNames, Piece, /*bAncestors*/ !bSwitchValue, HitMap))
				{
					OutWhy = FString::Printf(TEXT("путь «%s» — файлы %s"), *Piece, *HitMap);
					return true;
				}
			}
			return false;
		}

		// Командная строка запускает коммандлет-удалитель над защищаемой картой? OutCommandlet — имя класса, OutWhy — причина.
		bool FindPhysicalDeleteCommandletTarget(const TCHAR* Cmd, const TArray<FProtectedMap>& Maps, FString& OutCommandlet, FString& OutWhy)
		{
			TArray<FString> Tokens;
			TArray<FString> Switches;
			UCommandlet::ParseCommandLine(Cmd, Tokens, Switches); // тот же разбор, что у движка и коммандлетов
			FString CommandletToken;
			const FPhysicalDeleteCommandlet* Commandlet = FindPhysicalDeleteCommandlet(Tokens, Switches, CommandletToken);
			if (!Commandlet)
			{
				return false;
			}
			OutCommandlet = FString(Commandlet->Name) + TEXT("Commandlet");
			const bool bBuilder = FCString::Stricmp(Commandlet->Name, TEXT("WorldPartitionBuilder")) == 0;
			const bool bConvert = FCString::Stricmp(Commandlet->Name, TEXT("WorldPartitionConvert")) == 0;
			const bool bExternalActors = FCString::Stricmp(Commandlet->Name, TEXT("ExternalActors")) == 0;

			// -DeleteSourceLevels: удаляются подуровни загруженной карты (:1376), её _BuiltData (:531), пакеты акторов (:1446, :1484)
			// — какие именно карты, до загрузки не известно.
			if (bConvert && Switches.Contains(TEXT("DeleteSourceLevels")))
			{
				OutWhy = FString::Printf(TEXT("-DeleteSourceLevels — удаляет подуровни и _BuiltData загруженной карты, набор при запуске не известен (%s)"), Commandlet->Where);
				return true;
			}
			for (const FString& Token : Tokens)
			{
				if (Token == CommandletToken || Token.TrimQuotes().EndsWith(TEXT(".uproject"), ESearchCase::IgnoreCase))
				{
					continue;
				}
				FString Why;
				if (ItemTargetsProtected(Token, Maps, bBuilder, /*bSwitchValue*/ false, Why))
				{
					OutWhy = FString::Printf(TEXT("аргумент %s (%s)"), *Why, Commandlet->Where);
					return true;
				}
			}
			for (const FString& Switch : Switches)
			{
				int32 Equals = INDEX_NONE;
				if (!Switch.FindChar(TEXT('='), Equals))
				{
					continue;
				}
				const FString Key = Switch.Left(Equals);
				const FString Value = Switch.RightChop(Equals + 1).TrimQuotes();
				if (Key.Equals(TEXT("run"), ESearchCase::IgnoreCase))
				{
					continue;
				}
				FString Why;
				// Список карт из файла — тот же разбор, что у коммандлета (ExternalActorsCommandlet.cpp:156-175). Нечитаемый файл
				// коммандлет сам отвергает до работы («Invalid map list filename», return 1).
				if (bExternalActors && Key.Equals(TEXT("MapListFile"), ESearchCase::IgnoreCase))
				{
					TArray<FString> Lines;
					if (FFileHelper::LoadFileToStringArray(Lines, *Value))
					{
						for (const FString& Line : Lines)
						{
							if (ItemTargetsProtected(Line, Maps, false, /*bSwitchValue*/ false, Why))
							{
								OutWhy = FString::Printf(TEXT("-MapListFile «%s»: %s (%s)"), *Value, *Why, Commandlet->Where);
								return true;
							}
						}
					}
				}
				if (ItemTargetsProtected(Value, Maps, false, /*bSwitchValue*/ true, Why))
				{
					OutWhy = FString::Printf(TEXT("ключ -%s: %s (%s)"), *Key, *Why, Commandlet->Where);
					return true;
				}
			}
			return false;
		}

		// Один случай самотеста разбора командной строки.
		struct FCmdLineCase
		{
			const TCHAR* CommandLine;   // как FCommandLine::Get() (без пути exe)
			bool bExpectAutomatic;      // ожидаемый результат CollectCommandLineReasons
		};

		// Набор строк ревизии 15.09 + прежние признаки и граничные случаи разбора -ExecCmds.
		const FCmdLineCase SelfTestCases[] =
		{
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\""), false },
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" -log -skipcompile"), false },
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" -ExecCmds=\"stat fps\""), false },
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" -ExecCmds=\"py script.py\""), true },
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" -ExecCmds=\"stat fps,py x.py\""), true },
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" -EXEC=cmds.txt"), true },
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" -AutomatedTests"), true },
			// Дополнительно: несколько команд без Python, Python вторым ключом, «Automation», слово-двойник, прежние признаки.
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" -ExecCmds=\"stat unit, r.ScreenPercentage 50\" -log"), false },
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" -ExecCmds=\"stat fps\" -ExecCmds=\"py x.py\""), true },
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" -ExecCmds=\"stat fps, py.exec x\""), true },
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" -ExecCmds=\"Automation RunTests Asda;Quit\""), true },
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" -ExecCmds=\"pyramid 1\""), false },
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" -ExecCmds=py"), true },
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" -ExecutePythonScript=\"D:/x.py\""), true },
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" -run=pythonscript -script=x.py"), true },
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" -RenderOffscreen"), true },
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" -nullrhi"), true },
			// ★ 16.09.2026 (ревизия М5): исполнители файлов команд.
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" -ExecCmds=\"EXECFILE D:/cmds.txt\""), true },
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" -ExecCmds=\"stat fps, exec cmds.txt\""), true },
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" -ExecCmds=\"execfile\""), true },
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" -ExecCmds=\"stat fps\" -ExecCmds=\"Exec 'x y.txt'\""), true },
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" -ExecCmds=\"execute_thing 1\""), false },
			{ TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" -ExecCmds=\"EXECFILEX 1, executor\""), false },
		};

		// Самотест отказа запуска коммандлетов С1 на наборе {Login}: каждая строка — «ok/FAIL», возвращает пройденные/всего.
		void RunCommandletSelfTest(int32& InOutPassed, int32& InOutTotal)
		{
			TArray<FProtectedMap> Maps;
			FProtectedMap Login;
			if (!MakeProtectedMap(LoginPackage, Login))
			{
				++InOutTotal;
				UE_LOG(LogAsdaLoginGuard, Error, TEXT("[LOGIN-GUARD][SELFTEST] FAIL: не удалось собрать набор %s"), LoginPackage);
				return;
			}
			Maps.Add(Login);

			const FString P = TEXT("\"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/AsdaReborn.uproject\" ");
			const FString Content = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()); // D:/…/Content/
			const FString ContentWin = Content.Replace(TEXT("/"), TEXT("\\"));
			const FString Drive = Content.Left(2);
			const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()); // D:/…/AsdaReborn/

			// Файлы списков карт для ExternalActors -MapListFile (удаляются в конце).
			const FString ListDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("LoginGuardSelfTest"));
			const FString ListLogin = ListDir / TEXT("maps_login.txt");
			const FString ListOther = ListDir / TEXT("maps_other.txt");
			FFileHelper::SaveStringToFile(TEXT("/Game/Temp/_A\n/Game/Map/Login\n"), *ListLogin);
			FFileHelper::SaveStringToFile(TEXT("/Game/Temp/_A\n/Game/Temp/_B\n"), *ListOther);

			struct FCase
			{
				FString CommandLine; // как FCommandLine::Get()
				bool bExpectRefuse;  // ожидается отказ запуска
			};
			const TArray<FCase> Cases =
			{
				{ P + TEXT("-run=WorldPartitionConvert /Game/Map/Login -AllowCommandletRendering"), true },
				{ P + TEXT("-run=WorldPartitionConvertCommandlet Login -AllowCommandletRendering"), true },
				{ P + TEXT("-run=/Script/UnrealEd.WorldPartitionConvertCommandlet /Game/Map/Login.Login"), true },
				{ P + TEXT("WorldPartitionConvertCommandlet /Game/Map/Login -AllowCommandletRendering"), true },
				{ P + TEXT("-run=worldpartitionconvert \"") + Content + TEXT("Map/Login.umap\""), true },
				{ P + TEXT("-run=WorldPartitionConvert ") + Content.RightChop(2) + TEXT("Map/Login.umap"), true },
				{ P + TEXT("-run=WorldPartitionConvert ") + Drive + TEXT("..\\Content\\Map\\Login.umap"), true },
				{ P + TEXT("-run=WorldPartitionConvert /Game/Temp/_Other -AllowCommandletRendering"), false },
				{ P + TEXT("-run=WorldPartitionConvert /Game/Map/LoginX -AllowCommandletRendering"), false },
				{ P + TEXT("-run=WorldPartitionConvert /Game/Temp/_Other -AllowCommandletRendering -DeleteSourceLevels"), true },
				{ P + TEXT("-run=WorldPartitionConvert /Game/Temp/_Other -DataLayerAssetFolder=/Game/Map/"), true },
				{ P + TEXT("-run=ExternalActors \"") + ContentWin + TEXT("Map\\Login.umap\" -repair"), true },
				{ P + TEXT("-run=ExternalActors /Game/Temp/_Other -repair"), false },
				{ P + TEXT("-run=ExternalActors -MapListFile=\"") + ListLogin + TEXT("\" -disable"), true },
				{ P + TEXT("-run=ExternalActors -MapListFile=\"") + ListOther + TEXT("\" -disable"), false },
				{ P + TEXT("-run=WorldPartitionBuilderCommandlet * -Builder=WorldPartitionStaticLightingBuilder"), true },
				{ P + TEXT("-run=WorldPartitionBuilderCommandlet \"*,100\" -Builder=WorldPartitionStaticLightingBuilder"), true },
				{ P + TEXT("-run=WorldPartitionBuilder /Game/Map -Builder=WorldPartitionResaveActorsBuilder"), true },
				{ P + TEXT("-run=WorldPartitionBuilder /Game -Builder=WorldPartitionResaveActorsBuilder"), true },
				{ P + TEXT("-run=WorldPartitionBuilder /Game/__ExternalActors__/Map/Login -Builder=WorldPartitionResaveActorsBuilder"), true },
				{ P + TEXT("-run=WorldPartitionBuilderCommandlet /Game/Temp/_A,/Game/Temp/_B -Builder=WorldPartitionResaveActorsBuilder"), false },
				{ P + TEXT("-run=WorldPartitionBuilderCommandlet /Game/Temp/_A,Login -Builder=WorldPartitionResaveActorsBuilder"), true },
				{ P + TEXT("-run=WorldPartitionBuilderCommandlet _AsdaNoSuchCollection_ -Builder=WorldPartitionResaveActorsBuilder"), false },
				{ P + TEXT("-run=ConvertLevelsToExternalActors /Game/Map/Login -repair"), true },
				{ P + TEXT("-run=ConvertLevelsToExternalActorsCommandlet Login_BuiltData"), true },
				{ P + TEXT("-run=ConvertLevelsToExternalActors /Game/Temp/_Other -repair"), false },
				{ P + TEXT("-run=ResavePackages /Game/Map/Login"), false },
				{ P + TEXT("-run=pythonscript -script=\"D:/x.py\" /Game/Map/Login"), false },
				// ★ 16.09.2026 (ревизия М1): первый не-ключ — кандидат в коммандлеты, ТОЛЬКО когда нет «-run=» и нет токена «…Commandlet».
				{ P + TEXT("-run=pythonscript -script=\"D:/x.py\" ExternalActors /Game/Map/Login"), false },
				{ P + TEXT("-run=ResavePackages WorldPartitionConvert /Game/Map/Login"), false },
				{ P + TEXT("WorldPartitionConvert /Game/Map/Login"), true },
				{ P + TEXT("-run=pythonscript ExternalActorsCommandlet /Game/Map/Login"), true }, // токен «…Commandlet» сильнее «-run=»
				// ★ 16.09.2026 (ревизия М2): значения ключей — без ложных отказов по чужому расширению и по папке-родителю.
				{ P + TEXT("-run=WorldPartitionConvert /Game/Temp/_Other -log=Login.txt"), false },
				{ P + TEXT("-run=WorldPartitionConvert /Game/Temp/_Other -ProjectDir=") + ProjectDir, false },
				{ P + TEXT("-run=WorldPartitionConvert /Game/Temp/_Other -abslog=\"") + Content + TEXT("Saved/x.log\""), false },
				{ P + TEXT("-run=WorldPartitionConvert /Game/Temp/_Other -abslog=\"") + Content + TEXT("Map/Login.umap\""), true },
				{ P + TEXT("-run=WorldPartitionConvert /Game/Temp/_Other -X=Login.umap"), true },
				{ P + TEXT("-run=WorldPartitionConvert /Game/Temp/_Other -X=Login"), true },
				{ P + TEXT("-run=WorldPartitionConvert Login.txt"), true }, // аргумент-карта: SearchForPackageOnDisk ищет «Login.*»
				// ★ 16.09.2026 (ревизия §9, МЕЛК-1): пакет и ФАЙЛ, чьё имя равно пути папки OFPA карты.
				{ P + TEXT("-run=WorldPartitionConvert /Game/__ExternalObjects__/Map/Login"), true },
				{ P + TEXT("-run=ExternalActors \"") + Content + TEXT("__ExternalActors__/Map/Login.uasset\""), true },
				{ P + TEXT("-run=ExternalActors \"") + Content + TEXT("__ExternalActors__/Map/LoginOther.uasset\""), false },
				{ P + TEXT("-run=ExternalActors \"") + Content + TEXT("__ExternalActors__/Map/Login_BuiltData.uasset\""), false },
			};
			for (const FCase& Case : Cases)
			{
				FString Commandlet;
				FString Why;
				const bool bRefuse = FindPhysicalDeleteCommandletTarget(*Case.CommandLine, Maps, Commandlet, Why);
				++InOutTotal;
				if (bRefuse == Case.bExpectRefuse)
				{
					++InOutPassed;
					UE_LOG(LogAsdaLoginGuard, Display, TEXT("[LOGIN-GUARD][SELFTEST] ok: [%s] -> отказ запуска=%s %s%s"),
						*Case.CommandLine, bRefuse ? TEXT("да") : TEXT("нет"), *Commandlet, bRefuse ? *(TEXT(": ") + Why) : TEXT(""));
				}
				else
				{
					UE_LOG(LogAsdaLoginGuard, Error, TEXT("[LOGIN-GUARD][SELFTEST] FAIL: [%s] -> отказ запуска=%s, ожидалось %s %s%s"),
						*Case.CommandLine, bRefuse ? TEXT("да") : TEXT("нет"), Case.bExpectRefuse ? TEXT("да") : TEXT("нет"),
						*Commandlet, bRefuse ? *(TEXT(": ") + Why) : TEXT(""));
				}
			}
			IFileManager::Get().DeleteDirectory(*ListDir, false, true);
		}

		// Самотест разбора: каждый случай — строка «ok/FAIL» (FAIL = Error), в конце итог «N/M».
		void RunCommandLineSelfTest()
		{
			int32 Passed = 0;
			int32 Total = 0;
			for (const FCmdLineCase& Case : SelfTestCases)
			{
				TArray<FString> Reasons;
				CollectCommandLineReasons(Case.CommandLine, Reasons);
				const bool bAutomatic = Reasons.Num() > 0;
				const bool bOk = bAutomatic == Case.bExpectAutomatic;
				++Total;
				if (bOk)
				{
					++Passed;
					UE_LOG(LogAsdaLoginGuard, Display, TEXT("[LOGIN-GUARD][SELFTEST] ok: [%s] -> автоматика=%s (%s)"),
						Case.CommandLine, bAutomatic ? TEXT("да") : TEXT("нет"), *FString::Join(Reasons, TEXT(", ")));
				}
				else
				{
					UE_LOG(LogAsdaLoginGuard, Error, TEXT("[LOGIN-GUARD][SELFTEST] FAIL: [%s] -> автоматика=%s, ожидалось %s (%s)"),
						Case.CommandLine, bAutomatic ? TEXT("да") : TEXT("нет"), Case.bExpectAutomatic ? TEXT("да") : TEXT("нет"),
						*FString::Join(Reasons, TEXT(", ")));
				}
			}
			RunCommandletSelfTest(Passed, Total);
			UE_LOG(LogAsdaLoginGuard, Display, TEXT("[LOGIN-GUARD][SELFTEST] итог %d/%d"), Passed, Total);
		}

		// ★ 16.09.2026 (общий список). Сторожевой набор «пакеты сцены логина»: записи самих уровней заменяются на поддеревья
		//   (сам пакет уровня и его _BuiltData держит НАБОР КАРТЫ и ASDA_ALLOW_LOGIN_MAP_WRITE), папки внешних акторов/объектов
		//   уровня выбрасываются совсем (их целиком держит тот же набор карты), стендовые пакеты добавляются деревьями.
		// ★ 16.09.2026 (ревизия §9, МЕЛК-1): Drop для двух записей OFPA СОХРАНЁН, а дыра закрыта в наборе КАРТЫ
		//   (FindProtectedIn сверяет «имя + /», MakeProtectedFiles кладёт папки OFPA ещё и в основы файлов). Почему не SubTree,
		//   как предлагала ревизия: SubTree — это «path/**» БЕЗ самого path, то есть (а) сам путь папки так и остался бы ничьим,
		//   (б) содержимое папок OFPA уехало бы в набор пакетов логина, и при ASDA_ALLOW_LOGIN_MAP_WRITE=1 запись внешних
		//   акторов уровня отказывалась бы — это ломает разделение разрешений (прогоны r7/r8/r13 раздела 9.4).
		//   Классификация после правки совпадает с Python: объединение наборов = «логин» (самотест asda.LoginGuard.SelfTestPackageList).
		void BuildAssetMatcher(const AsdaLoginPackageList::FParsedList& Parsed, const TArray<FProtectedMap>& Maps,
			const TArray<FString>& ExtraTrees, AsdaLoginPackageList::FMatcher& Out)
		{
			TArray<FString> SubTreeOnly;
			TArray<FString> DropPaths;
			for (const FProtectedMap& Map : Maps)
			{
				SubTreeOnly.Add(Map.Package);
				SubTreeOnly.Add(Map.BuiltData);
				DropPaths.Add(Map.ExternalActorsRoot.LeftChop(1));  // без хвостового «/»
				DropPaths.Add(Map.ExternalObjectsRoot.LeftChop(1));
			}
			Out.Build(Parsed, SubTreeOnly, DropPaths);
			for (const FString& Tree : ExtraTrees)
			{
				Out.AddTree(Tree);
			}
			Out.Finalize();
		}

		// Один случай самотеста разбора JSON: имя, байты, ожидается ли успех.
		struct FListCase
		{
			FString Name;
			TArray<uint8> Bytes;
			bool bExpectOk;
		};

		// Байты из строки (UTF-8 без BOM) — как пишет генератор списка.
		TArray<uint8> Utf8(const FString& Text)
		{
			FTCHARToUTF8 Converted(*Text);
			TArray<uint8> Bytes;
			Bytes.Append(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
			return Bytes;
		}

		// ★ 16.09.2026 (общий список). Самотест чтения общего JSON и сверки путей по нему. Файлы случаев кладутся в
		//   Saved/LoginGuardSelfTest/json (аргумент «keep» — оставить их для сверки тем же разбором в Python).
		void RunPackageListSelfTest(const TArray<FString>& Args)
		{
			const bool bKeep = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("keep"), ESearchCase::IgnoreCase); });
			const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("LoginGuardSelfTest") / TEXT("json"));
			IFileManager::Get().DeleteDirectory(*Dir, false, true);

			const FString ListPath = AsdaLoginPackageList::GetProjectListPath();
			TArray<uint8> RealBytes;
			FFileHelper::LoadFileToArray(RealBytes, *ListPath);
			const FString Minimal = TEXT("{\"format\":\"asda_login_packages\",\"version\":1,\"model_dirs\":[],")
				TEXT("\"packages\":[{\"path\":\"/Game/Map/Login\",\"match\":\"tree\",\"note\":\"уровень\"},")
				TEXT("{\"path\":\"/Game/T/Pre\",\"match\":\"prefix\"}],")
				TEXT("\"shared_exceptions\":[{\"path\":\"/Game/Map/Login/Shared\",\"match\":\"exact\"}]}");
			auto Replace = [&Minimal](const TCHAR* From, const TCHAR* To) { return Minimal.Replace(From, To); };

			TArray<FListCase> Cases;
			Cases.Add({ TEXT("00_real"), RealBytes, true });
			{
				TArray<uint8> Bom = { 0xEF, 0xBB, 0xBF };
				Bom.Append(RealBytes);
				Cases.Add({ TEXT("01_bom"), Bom, false });
			}
			Cases.Add({ TEXT("02_empty"), TArray<uint8>(), false });
			{
				TArray<uint8> Cut = RealBytes;
				Cut.SetNum(FMath::Min(200, Cut.Num()));
				Cases.Add({ TEXT("03_truncated"), Cut, false });
			}
			Cases.Add({ TEXT("04_trailing_garbage"), Utf8(Minimal + TEXT("\n}")), false });
			Cases.Add({ TEXT("05_other_format"), Utf8(Replace(TEXT("asda_login_packages"), TEXT("asda_other"))), false });
			Cases.Add({ TEXT("06_version2"), Utf8(Replace(TEXT("\"version\":1"), TEXT("\"version\":2"))), false });
			Cases.Add({ TEXT("07_packages_not_array"), Utf8(Replace(TEXT("\"packages\":["), TEXT("\"packages\":{\"x\":["))), false });
			Cases.Add({ TEXT("08_no_shared_exceptions"), Utf8(Replace(TEXT("\"shared_exceptions\""), TEXT("\"other_key\""))), false });
			Cases.Add({ TEXT("09_star_in_path"), Utf8(Replace(TEXT("/Game/T/Pre"), TEXT("/Game/T/Pre*"))), false });
			Cases.Add({ TEXT("10_unknown_match"), Utf8(Replace(TEXT("\"match\":\"prefix\""), TEXT("\"match\":\"glob\""))), false });
			Cases.Add({ TEXT("11_prefix_in_exceptions"), Utf8(Replace(TEXT("\"match\":\"exact\""), TEXT("\"match\":\"prefix\""))), false });
			Cases.Add({ TEXT("12_engine_path"), Utf8(Replace(TEXT("/Game/T/Pre"), TEXT("/Engine/T/Pre"))), false });
			Cases.Add({ TEXT("13_duplicate"), Utf8(Replace(TEXT("{\"path\":\"/Game/T/Pre\",\"match\":\"prefix\"}"),
				TEXT("{\"path\":\"/Game/T/Pre\",\"match\":\"prefix\"},{\"path\":\"/Game/T/Pre\",\"match\":\"prefix\"}"))), false });
			Cases.Add({ TEXT("14_no_login_entry"), Utf8(Replace(TEXT("/Game/Map/Login\",\"match\":\"tree"), TEXT("/Game/Map/LoginX\",\"match\":\"tree"))), false });
			Cases.Add({ TEXT("15_empty_arrays"), Utf8(TEXT("{\"format\":\"asda_login_packages\",\"version\":1,\"model_dirs\":[],\"packages\":[],\"shared_exceptions\":[]}")), false });
			Cases.Add({ TEXT("16_backslash_in_path"), Utf8(Replace(TEXT("/Game/T/Pre"), TEXT("/Game/T\\\\Pre"))), false });
			Cases.Add({ TEXT("17_trailing_slash"), Utf8(Replace(TEXT("/Game/T/Pre"), TEXT("/Game/T/Pre/"))), false });
			Cases.Add({ TEXT("18_minimal_ok"), Utf8(Minimal), true });
			Cases.Add({ TEXT("19_version_float"), Utf8(Replace(TEXT("\"version\":1"), TEXT("\"version\":1.0"))), true });
			{
				TArray<uint8> Bad = Utf8(Minimal);
				Bad.Insert(0xC3, 1); // оборванная последовательность UTF-8
				Cases.Add({ TEXT("20_bad_utf8"), Bad, false });
			}

			int32 Passed = 0;
			int32 Total = 0;
			TArray<FString> Results;
			for (const FListCase& Case : Cases)
			{
				const FString File = Dir / (Case.Name + TEXT(".json"));
				FFileHelper::SaveArrayToFile(Case.Bytes, *File);
				AsdaLoginPackageList::FParsedList Parsed;
				FString Error;
				const bool bOk = AsdaLoginPackageList::ParseBytes(Case.Bytes, Parsed, Error);
				++Total;
				Results.Add(FString::Printf(TEXT("%s\t%s\t%s"), *Case.Name, bOk ? TEXT("ok") : TEXT("err"), *Error));
				if (bOk == Case.bExpectOk)
				{
					++Passed;
					UE_LOG(LogAsdaLoginGuard, Display, TEXT("[LOGIN-GUARD][LIST-SELFTEST] ok: %s -> %s%s%s"),
						*Case.Name, bOk ? TEXT("разобран") : TEXT("ошибка"), bOk ? TEXT("") : TEXT(": "), bOk ? TEXT("") : *Error);
				}
				else
				{
					UE_LOG(LogAsdaLoginGuard, Error, TEXT("[LOGIN-GUARD][LIST-SELFTEST] FAIL: %s -> %s (%s), ожидалось %s"),
						*Case.Name, bOk ? TEXT("разобран") : TEXT("ошибка"), *Error, Case.bExpectOk ? TEXT("разобран") : TEXT("ошибка"));
				}
			}

			// Сверка путей по разобранному настоящему списку: формы пути, дерево, префикс, исключение, fail-closed.
			AsdaLoginPackageList::FParsedList Real;
			FString RealError;
			AsdaLoginPackageList::FMatcher Matcher;
			const bool bRealOk = AsdaLoginPackageList::ParseBytes(RealBytes, Real, RealError);
			if (bRealOk)
			{
				Matcher.Build(Real, {}, {});
				Matcher.Finalize();
			}
			struct FPathCase
			{
				FString Path;
				AsdaLoginPackageList::EOwnership Expect;
			};
			const FString ContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
			const TArray<FPathCase> PathCases =
			{
				{ TEXT("/Game/Map/Login"), AsdaLoginPackageList::EOwnership::Owned },
				{ TEXT("/Game/Map/Login.Login"), AsdaLoginPackageList::EOwnership::Owned },
				// ★ 16.09.2026 (ревизия §9, МЕЛК-1): путь папки OFPA как ПАКЕТ и как файл на диске — по списку это логин.
				{ TEXT("/Game/__ExternalActors__/Map/Login"), AsdaLoginPackageList::EOwnership::Owned },
				{ TEXT("/Game/__ExternalObjects__/Map/Login/A_UAID_0"), AsdaLoginPackageList::EOwnership::Owned },
				{ ContentDir + TEXT("__ExternalActors__/Map/Login.uasset"), AsdaLoginPackageList::EOwnership::Owned },
				{ TEXT("/Game/__ExternalActors__/Map/Login_BuiltData"), AsdaLoginPackageList::EOwnership::None },
				{ TEXT("/Game/__ExternalActors__/Map/LoginOther"), AsdaLoginPackageList::EOwnership::None },
				{ TEXT("/Game/map/LOGIN"), AsdaLoginPackageList::EOwnership::Owned },
				{ TEXT("/All/Game/Map/Login"), AsdaLoginPackageList::EOwnership::Owned },
				{ TEXT("Game/Map/Login"), AsdaLoginPackageList::EOwnership::Owned },
				{ TEXT("World'/Game/Map/Login.Login'"), AsdaLoginPackageList::EOwnership::Owned },
				{ TEXT("/Script/Engine.World'/Game/Map/Login.Login'"), AsdaLoginPackageList::EOwnership::Owned },
				{ ContentDir + TEXT("Map/Login.umap"), AsdaLoginPackageList::EOwnership::Owned },
				{ TEXT("/Game/Map/HolyLight_SpotLight"), AsdaLoginPackageList::EOwnership::Owned },
				{ TEXT("/Game/Map/Login_Shine"), AsdaLoginPackageList::EOwnership::Owned },
				{ TEXT("/Game/Map/Shine_Dust"), AsdaLoginPackageList::EOwnership::Owned },
				{ TEXT("/Game/AsdaAssets/UI/Login/WBP_LOGIN_WIN"), AsdaLoginPackageList::EOwnership::Owned },
				{ TEXT("/Game/AsdaAssets/UI/Login/WBP_AD_Win"), AsdaLoginPackageList::EOwnership::Exception },
				{ TEXT("/Game/AsdaAssets/UI/Login/WBP_MESSAGE2_WIN"), AsdaLoginPackageList::EOwnership::Exception },
				{ TEXT("/Game/AsdaAssets/Models/Char/cre_creation01-a_00"), AsdaLoginPackageList::EOwnership::Owned }, // префикс
				{ TEXT("/Game/AsdaAssets/Models/Char/cre_creation02"), AsdaLoginPackageList::EOwnership::None },
				{ TEXT("/Game/Map/LoginX"), AsdaLoginPackageList::EOwnership::None },
				{ TEXT("/Game/Map/Alpen"), AsdaLoginPackageList::EOwnership::None },
				{ TEXT("/Engine/BasicShapes/Plane"), AsdaLoginPackageList::EOwnership::None },
				{ TEXT("/Game"), AsdaLoginPackageList::EOwnership::None },
			};
			for (const FPathCase& Case : PathCases)
			{
				const AsdaLoginPackageList::EOwnership Got = Matcher.Classify(Case.Path);
				++Total;
				if (Got == Case.Expect)
				{
					++Passed;
				}
				else
				{
					UE_LOG(LogAsdaLoginGuard, Error, TEXT("[LOGIN-GUARD][LIST-SELFTEST] FAIL (путь): %s -> %d, ожидалось %d"),
						*Case.Path, int32(Got), int32(Case.Expect));
				}
			}
			// Fail-closed: всё под /Game — логин, остальное — нет.
			AsdaLoginPackageList::FMatcher Closed;
			Closed.SetFailClosed(TEXT("самотест"));
			Closed.Finalize();
			const TArray<FPathCase> ClosedCases =
			{
				{ TEXT("/Game/Temp/Anything"), AsdaLoginPackageList::EOwnership::Owned },
				{ TEXT("/Game"), AsdaLoginPackageList::EOwnership::Owned },
				{ TEXT("/Engine/BasicShapes/Plane"), AsdaLoginPackageList::EOwnership::None },
				{ TEXT("/Temp/Untitled_0"), AsdaLoginPackageList::EOwnership::None },
			};
			for (const FPathCase& Case : ClosedCases)
			{
				const AsdaLoginPackageList::EOwnership Got = Closed.Classify(Case.Path);
				++Total;
				if (Got == Case.Expect)
				{
					++Passed;
				}
				else
				{
					UE_LOG(LogAsdaLoginGuard, Error, TEXT("[LOGIN-GUARD][LIST-SELFTEST] FAIL (fail-closed): %s -> %d, ожидалось %d"),
						*Case.Path, int32(Got), int32(Case.Expect));
				}
			}

			// ★ 16.09.2026 (ревизия §9, МЕЛК-1). Разделение наборов на НАСТОЯЩЕМ списке: что держит набор карты (уровень,
			//   ASDA_ALLOW_LOGIN_MAP_WRITE), что — набор пакетов логина (ASDA_ALLOW_LOGIN_WRITE); и совпадает ли ОБЪЕДИНЕНИЕ
			//   наборов с «логином» по списку, как его читает Python. Набор карты локальный: состояние установки не участвует.
			if (bRealOk)
			{
				TArray<FProtectedMap> LocalMaps;
				FProtectedMap LocalLogin;
				if (MakeProtectedMap(LoginPackage, LocalLogin))
				{
					LocalMaps.Add(LocalLogin);
				}
				AsdaLoginPackageList::FMatcher Enforce;
				BuildAssetMatcher(Real, LocalMaps, {}, Enforce);
				struct FKindCase
				{
					FString Path;
					EProtectKind Expect;
				};
				const TArray<FKindCase> KindCases =
				{
					{ TEXT("/Game/Map/Login"), EProtectKind::Map },
					{ TEXT("/Game/Map/Login_BuiltData"), EProtectKind::Map },
					{ TEXT("/Game/__ExternalActors__/Map/Login"), EProtectKind::Map },   // ← дыра МЕЛК-1: было «ничьё»
					{ TEXT("/Game/__ExternalObjects__/Map/Login"), EProtectKind::Map },  // ← дыра МЕЛК-1: было «ничьё»
					{ TEXT("/Game/__ExternalActors__/Map/Login/A/B_UAID_0"), EProtectKind::Map },
					{ TEXT("/Game/__ExternalObjects__/Map/Login/A"), EProtectKind::Map },
					// Дисковых путей здесь нет намеренно: ClassifyPackage получает ДЛИННЫЕ имена пакетов (файл сохранения
					// переводит в имя TryConvertFilenameToLongPackageName, файлы сверяет AsdaLoginFileGuard). Форма
					// «<Content>/…/Login.uasset» проверена выше на самом списке (PathCases) и в SelfTestFilePaths.
					{ TEXT("/Game/Map/Login/Sub"), EProtectKind::Asset },                // папка с именем уровня — не сам уровень
					{ TEXT("/Game/Map/HolyLight_SpotLight"), EProtectKind::Asset },
					{ TEXT("/Game/AsdaAssets/UI/Login/WBP_AD_Win"), EProtectKind::None },// shared_exceptions
					{ TEXT("/Game/__ExternalActors__/Map/Login_BuiltData"), EProtectKind::None },
					{ TEXT("/Game/__ExternalActors__/Map/LoginOther"), EProtectKind::None },
					{ TEXT("/Game/Map/Alpen"), EProtectKind::None },
				};
				for (const FKindCase& Case : KindCases)
				{
					FString Who;
					const EProtectKind Kind = ClassifyPackageIn(LocalMaps, &Enforce, Case.Path, Who);
					++Total;
					if (Kind == Case.Expect)
					{
						++Passed;
					}
					else
					{
						UE_LOG(LogAsdaLoginGuard, Error, TEXT("[LOGIN-GUARD][LIST-SELFTEST] FAIL (набор): %s -> %d, ожидалось %d"),
							*Case.Path, int32(Kind), int32(Case.Expect));
					}
					// Объединение наборов обязано совпадать со списком (исключения shared_exceptions — на стороне «не логин»).
					const bool bInList = Matcher.Classify(Case.Path) == AsdaLoginPackageList::EOwnership::Owned;
					++Total;
					if ((Kind != EProtectKind::None) == bInList)
					{
						++Passed;
					}
					else
					{
						UE_LOG(LogAsdaLoginGuard, Error, TEXT("[LOGIN-GUARD][LIST-SELFTEST] FAIL (сверка со списком): %s -> наборы %s, список %s"),
							*Case.Path, Kind != EProtectKind::None ? TEXT("защищён") : TEXT("нет"), bInList ? TEXT("логин") : TEXT("нет"));
					}
				}
			}

			FFileHelper::SaveStringArrayToFile(Results, *(Dir / TEXT("results.tsv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			UE_LOG(LogAsdaLoginGuard, Display, TEXT("[LOGIN-GUARD][LIST-SELFTEST] итог %d/%d; список %s (%s); файлы случаев %s"),
				Passed, Total, *ListPath, *Matcher.Describe(), bKeep ? *Dir : TEXT("удалены"));
			if (!bKeep)
			{
				IFileManager::Get().DeleteDirectory(*Dir, false, true);
			}
		}

		// ★ 16.09.2026 (общий список). Выгрузка множества «пакет логина» по ВСЕМУ листингу Content — для сверки 1:1 с Python
		//   (scratchpad/cmp_login_packages.py). Заодно сверяются сторожевой набор (файловый слой) и быстрый разбор путей.
		void RunDumpLoginPackages(const TArray<FString>& Args)
		{
			const FString OutFile = Args.Num() > 0 ? Args[0] : FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("LoginGuardDump.txt"));
			AsdaLoginPackageList::FParsedList Parsed;
			FString Error;
			const bool bLoaded = AsdaLoginPackageList::LoadFile(AsdaLoginPackageList::GetProjectListPath(), Parsed, Error);
			// Список КАК ЕГО ЧИТАЕТ PYTHON (без замен записей уровня и без стендовых пакетов).
			AsdaLoginPackageList::FMatcher Raw;
			// Сторожевой набор (как при установке: уровень Login держит набор карты).
			AsdaLoginPackageList::FMatcher Enforce;
			TArray<FProtectedMap> Maps;
			FProtectedMap Login;
			if (MakeProtectedMap(LoginPackage, Login))
			{
				Maps.Add(Login);
			}
			if (bLoaded)
			{
				Raw.Build(Parsed, {}, {});
				Raw.Finalize();
				BuildAssetMatcher(Parsed, Maps, {}, Enforce);
			}
			else
			{
				UE_LOG(LogAsdaLoginGuard, Error, TEXT("[LOGIN-GUARD][DUMP] список не прочитан (%s) — fail-closed"), *Error);
				Raw.SetFailClosed(Error);
				Raw.Finalize();
				Enforce.SetFailClosed(Error);
				Enforce.Finalize();
			}

			AsdaLoginFileGuard::FRules Rules;
			for (const FProtectedMap& Map : Maps)
			{
				Rules.MapPackages.Add(Map.Package);
			}
			Rules.AssetMatcher = &Enforce;
			Rules.bMapGuard = true;
			Rules.bAssetGuard = true;

			TArray<FString> Lines;
			int32 Total = 0;
			int32 Owned = 0;
			int32 FileMismatch = 0;
			int32 FastMismatch = 0;
			const double Start = FPlatformTime::Seconds();
			AsdaLoginFileGuard::ForEachContentPackage(Rules, [&](const FString& Package, bool bFileProtected, bool bFastOk)
			{
				const bool bOwned = Raw.Classify(Package) == AsdaLoginPackageList::EOwnership::Owned;
				++Total;
				Owned += bOwned ? 1 : 0;
				if (bOwned != bFileProtected)
				{
					++FileMismatch;
					if (FileMismatch <= 20)
					{
						UE_LOG(LogAsdaLoginGuard, Error, TEXT("[LOGIN-GUARD][DUMP] расхождение: %s — список=%s, файловый слой=%s"),
							*Package, bOwned ? TEXT("логин") : TEXT("нет"), bFileProtected ? TEXT("защищён") : TEXT("нет"));
					}
				}
				if (!bFastOk)
				{
					++FastMismatch;
					if (FastMismatch <= 20)
					{
						UE_LOG(LogAsdaLoginGuard, Error, TEXT("[LOGIN-GUARD][DUMP] быстрый разбор пути разошёлся с полным: %s"), *Package);
					}
				}
				Lines.Add(Package + TEXT("\t") + (bOwned ? TEXT("1") : TEXT("0")));
			});
			Lines.Sort();
			const bool bSaved = FFileHelper::SaveStringArrayToFile(Lines, *OutFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			UE_LOG(LogAsdaLoginGuard, Display, TEXT("[LOGIN-GUARD][DUMP] пакетов %d, из них логина %d; расхождений со сторожевым набором %d, быстрого разбора %d; %.1f с; файл %s (%s); список %s"),
				Total, Owned, FileMismatch, FastMismatch, FPlatformTime::Seconds() - Start, *OutFile,
				bSaved ? TEXT("записан") : TEXT("НЕ записан"), *Raw.Describe());
		}

		// Консольная команда самотеста (запуск из стенда: unreal.SystemLibrary.execute_console_command).
		FAutoConsoleCommand SelfTestCommand(
			TEXT("asda.LoginGuard.SelfTestCmdLine"),
			TEXT("Самотест разбора командной строки защиты логина: какие строки запуска считаются автоматической сессией."),
			FConsoleCommandDelegate::CreateStatic(&RunCommandLineSelfTest));

		FAutoConsoleCommand PackageListSelfTestCommand(
			TEXT("asda.LoginGuard.SelfTestPackageList"),
			TEXT("Самотест чтения общего списка пакетов логина (Tools/data/asda_login_packages.json) и сверки путей по нему; «keep» — оставить файлы случаев."),
			FConsoleCommandWithArgsDelegate::CreateStatic(&RunPackageListSelfTest));

		FAutoConsoleCommand DumpLoginPackagesCommand(
			TEXT("asda.LoginGuard.DumpLoginPackages"),
			TEXT("Выгрузить множество пакетов сцены логина по всему Content в файл (для сверки с Python): asda.LoginGuard.DumpLoginPackages <файл>."),
			FConsoleCommandWithArgsDelegate::CreateStatic(&RunDumpLoginPackages));
	}

	void Install()
	{
		if (bInstalled)
		{
			return;
		}

		ProtectedMaps.Reset();
		FProtectedMap Login;
		if (MakeProtectedMap(LoginPackage, Login))
		{
			ProtectedMaps.Add(Login);
		}
		// Стенд: дополнительные защищаемые пакеты, чтобы проверять отказ, не трогая настоящий Login. ★ 16.09.2026: записей
		// может быть несколько (через «;»), «map:» — защищается как УРОВЕНЬ, «asset:» — как пакет сцены логина.
		TArray<FString> TestAssetPackages;
		{
			const FString TestValue = FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_LOGIN_GUARD_TEST_PACKAGE"));
			TArray<AsdaLoginFileGuard::FTestPackage> TestPackages;
			AsdaLoginFileGuard::ParseTestPackages(TestValue, TestPackages);
			for (const AsdaLoginFileGuard::FTestPackage& Item : TestPackages)
			{
				FProtectedMap Test;
				if (Item.bMap && MakeProtectedMap(Item.Package, Test))
				{
					ProtectedMaps.Add(Test);
				}
				else if (!Item.bMap && FPackageName::IsValidLongPackageName(Item.Package))
				{
					TestAssetPackages.Add(Item.Package);
				}
				else
				{
					UE_LOG(LogAsdaLoginGuard, Warning, TEXT("[LOGIN-GUARD] ASDA_LOGIN_GUARD_TEST_PACKAGE='%s' — не длинное имя пакета, пропущено"), *Item.Package);
				}
			}
		}

		DetectAutomaticSession();
		bMapWriteAllowedByEnv = IsEnvOne(MapWriteEnv);
		bAssetWriteEnvSet = IsEnvOne(AssetWriteEnv);

		// ★ 16.09.2026 (общий список). Пакеты сцены логина — из Tools/data/asda_login_packages.json (тот же файл, что у Python).
		//   Ошибка чтения/разбора → fail-closed: защищаемым считается ВСЁ под /Game. Набор живёт в куче до конца процесса.
		{
			const FString ListPath = AsdaLoginPackageList::GetProjectListPath();
			AsdaLoginPackageList::FParsedList Parsed;
			FString ListError;
			AsdaLoginPackageList::FMatcher* Matcher = new AsdaLoginPackageList::FMatcher();
			if (AsdaLoginPackageList::LoadFile(ListPath, Parsed, ListError))
			{
				BuildAssetMatcher(Parsed, ProtectedMaps, TestAssetPackages, *Matcher);
				ListStatus = FString::Printf(TEXT("прочитан: %s"), *Matcher->Describe());
			}
			else
			{
				Matcher->SetFailClosed(ListError);
				Matcher->Finalize();
				ListStatus = FString::Printf(TEXT("НЕ прочитан (%s) — fail-closed: ВСЁ под /Game"), *ListError);
				if (bAutomaticSession && !bAssetWriteEnvSet)
				{
					UE_LOG(LogAsdaLoginGuard, Error, TEXT("[LOGIN-GUARD] список пакетов сцены логина %s не прочитан (%s) — fail-closed: ВСЁ под /Game считается сценой логина (запись и удаление — только с %s=1)"),
						*ListPath, *ListError, AssetWriteEnv);
				}
				else
				{
					UE_LOG(LogAsdaLoginGuard, Warning, TEXT("[LOGIN-GUARD] список пакетов сцены логина %s не прочитан (%s); запрет сейчас не действует (сессия автоматическая=%s, %s=%s)"),
						*ListPath, *ListError, bAutomaticSession ? TEXT("да") : TEXT("нет"), AssetWriteEnv, bAssetWriteEnvSet ? TEXT("1") : TEXT("нет"));
				}
			}
			AssetMatcher = Matcher;
		}

		// ★ 16.09.2026 (ревизия §9, МЕЛК-3): отказ запуска коммандлета-удалителя (С1а) перенесён ВНИЗ, после установки сторожа.
		//   БЫЛО: проверка стояла здесь и при отказе делала return ДО привязки делегатов и файловой обёртки. Если бы
		//   RequestExitWithStatus(Force=true) вернул управление (TerminateProcess не сработал, будущая версия платформы),
		//   сессия осталась бы вовсе без сторожа. Теперь сторож стоит в любом случае, а отказ запуска идёт последним шагом.

		// Обернуть делегат сохранения: копия прежней привязки + своя статическая функция.
		PreviousDelegate = FCoreUObjectDelegates::IsPackageOKToSaveDelegate;
		FCoreUObjectDelegates::IsPackageOKToSaveDelegate.BindStatic(&IsPackageOKToSave);
		OurHandle = FCoreUObjectDelegates::IsPackageOKToSaveDelegate.GetHandle();

		// Запрет удаления (многоадресный делегат — просто подписка).
		CanDeleteHandle = FEditorDelegates::OnAssetsCanDelete.AddStatic(&OnAssetsCanDelete);
		bInstalled = true;

		TArray<FString> Names;
		for (const FProtectedMap& Map : ProtectedMaps)
		{
			Names.Add(Map.Package);
		}

		// ★ 15.09.2026: последний рубеж на уровне файловой системы UE (AsdaLoginFileGuard) — закрывает переименование
		//   (AssetRenameManager → CleanupAfterSuccessfulDelete → IFileManager::Delete) и прочие пути мимо делегатов.
		//   Ставится только когда запрет действует: в интерактивной сессии и при ASDA_ALLOW_LOGIN_MAP_WRITE=1 обёртки нет вовсе.
		if (IsGuardActive() || IsAssetGuardActive())
		{
			AsdaLoginFileGuard::FRules Rules;
			Rules.MapPackages = Names;
			Rules.AssetMatcher = AssetMatcher;
			Rules.bMapGuard = IsGuardActive();
			Rules.bAssetGuard = IsAssetGuardActive();
			AsdaLoginFileGuard::Install(Rules, AutomaticReason, PermissionHint(), AssetPermissionHint());
		}

		UE_LOG(LogAsdaLoginGuard, Display, TEXT("[LOGIN-GUARD] защита сохранения и удаления установлена: уровни [%s] (запрет=%s); пакеты сцены логина: запрет=%s, список %s%s; автоматическая сессия=%s%s; %s=%s; %s=%s; прежний обработчик=%s; файловый сторож=%s"),
			*FString::Join(Names, TEXT(", ")),
			IsGuardActive() ? TEXT("да") : TEXT("нет"),
			IsAssetGuardActive() ? TEXT("да") : TEXT("нет"),
			*ListStatus,
			TestAssetPackages.Num() > 0 ? *FString::Printf(TEXT("; стендовые пакеты [%s]"), *FString::Join(TestAssetPackages, TEXT(", "))) : TEXT(""),
			bAutomaticSession ? TEXT("да (") : TEXT("нет"),
			bAutomaticSession ? *(AutomaticReason + TEXT(")")) : TEXT(""),
			MapWriteEnv, bMapWriteAllowedByEnv ? TEXT("1") : TEXT("нет"),
			AssetWriteEnv, bAssetWriteEnvSet ? TEXT("1") : TEXT("нет"),
			PreviousDelegate.IsBound() ? TEXT("есть") : TEXT("нет"),
			AsdaLoginFileGuard::IsInstalled() ? TEXT("есть") : TEXT("нет"));

		// ★ 16.09.2026 (ревизия С1а): коммандлет-удалитель через физический слой над защищаемой картой — отказ ЗАПУСКА.
		//   Порядок движка: модули PostEngineInit (LaunchEngineLoop.cpp:4068-4069) → NewObject коммандлета (:4081) →
		//   Commandlet->Main (:4110); проверки IsEngineExitRequested между ними НЕТ (:3986 — только до InitEditor), поэтому
		//   RequestEngineExit Main не остановит. Выход немедленный: FPlatformMisc::RequestExitWithStatus(Force=true) сбрасывает
		//   лог (GLog->Flush) и завершает процесс TerminateProcess с кодом (WindowsPlatformMisc.cpp:1474-1515).
		// ★ 16.09.2026 (ревизия §9, МЕЛК-3): проверка стоит ПОСЛЕ установки сторожа (делегаты, подписка на удаление, файловая
		//   обёртка уже стоят). Если процесс почему-либо не завершится, сессия останется защищённой обычным порядком.
		if (IsGuardActive() && IsRunningCommandlet())
		{
			FString Commandlet;
			FString Why;
			if (FindPhysicalDeleteCommandletTarget(FCommandLine::Get(), ProtectedMaps, Commandlet, Why))
			{
				UE_LOG(LogAsdaLoginGuard, Error, TEXT("[LOGIN-GUARD] отказ запуска коммандлета %s в автоматической сессии: %s. Он удаляет файлы через физический слой (FPackageSourceControlHelper::Delete) мимо файлового сторожа — процесс завершается с кодом 1 до его работы (сессия: %s; %s)"),
					*Commandlet, *Why, *AutomaticReason, *PermissionHint());
				if (GLog)
				{
					GLog->Flush();
				}
				FPlatformMisc::RequestExitWithStatus(/*Force*/ true, /*ReturnCode*/ 1, TEXT("AsdaLoginSaveGuard: отказ запуска коммандлета"));
				return;
			}
		}
	}

	void Uninstall()
	{
		if (!bInstalled)
		{
			return;
		}
		// Вернуть прежний обработчик, только если после нас никто не перепривязал делегат.
		if (FCoreUObjectDelegates::IsPackageOKToSaveDelegate.GetHandle() == OurHandle)
		{
			FCoreUObjectDelegates::IsPackageOKToSaveDelegate = PreviousDelegate;
		}
		PreviousDelegate.Unbind();
		OurHandle.Reset();
		FEditorDelegates::OnAssetsCanDelete.Remove(CanDeleteHandle);
		CanDeleteHandle.Reset();
		// Снять файловую обёртку с цепочки платформенных файлов (если ставилась).
		AsdaLoginFileGuard::Uninstall();
		bInstalled = false;
	}

	void Shutdown()
	{
		if (!bInstalled)
		{
			return;
		}
		// Выход процесса: FEngineLoop::Exit → UnloadModulesAtShutdown (LaunchEngineLoop.cpp:5182) после RequestEngineExit;
		// DLL при этом не освобождается (ModuleManager.cpp:1372-1379). Делегаты и файловая обёртка остаются: статические
		// функции и объект обёртки живут до конца процесса, прежний обработчик UEditorEngine привязан через UObject и после
		// уничтожения редактора просто не исполняется (IsBound=false).
		if (IsEngineExitRequested())
		{
			UE_LOG(LogAsdaLoginGuard, Display, TEXT("[LOGIN-GUARD] выход процесса: защита сохранения/удаления и файловый сторож=%s остаются до конца процесса"),
				AsdaLoginFileGuard::IsInstalled() ? TEXT("есть") : TEXT("нет"));
			return;
		}
		// Выгрузка модуля во время работы: DLL будет освобождена — снять всё, что указывает на её код.
		Uninstall();
	}
}

#endif // WITH_EDITOR
