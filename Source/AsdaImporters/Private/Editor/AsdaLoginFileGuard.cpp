// Copyright (c) 2026 @sitxovski (Discord)
// AsdaLoginFileGuard — обёртка IPlatformFile: отказ ЗАПИСИ/УДАЛЕНИЯ/ПЕРЕМЕЩЕНИЯ файлов уровня логина (см. заголовок).
#include "Editor/AsdaLoginFileGuard.h"

#if WITH_EDITOR

#include "Editor/AsdaLoginPackageList.h"

#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/PackagePath.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Templates/UniquePtr.h"
#include <atomic>

#if PLATFORM_WINDOWS
// Тома «Volume{GUID}» → буква диска, развёртка коротких имён 8.3 (16.09.2026, ревизия М1).
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/MinWindows.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

// Своя категория (в unity-сборке имя не должно совпасть с LogAsdaLoginGuard из AsdaLoginSaveGuard.cpp).
DEFINE_LOG_CATEGORY_STATIC(LogAsdaLoginFileGuard, Log, All);

namespace AsdaLoginFileGuard
{
	namespace
	{
		// Имя слоя в цепочке (FPlatformFileManager::FindPlatformFile ищет по нему).
		const TCHAR* const WrapperName = TEXT("AsdaLoginFileGuard");

		// Настоящий уровень логина — пробе запрещено работать с ним даже при ошибочной переменной стенда.
		const TCHAR* const LoginPackage = TEXT("/Game/Map/Login");

		// ★ 16.09.2026 (ревизия М4): единственная папка, над пакетами которой разрешена разрушительная проба.
		const TCHAR* const ProbePackagePrefix = TEXT("/Game/Temp/_LoginGuardFS/");

		// Защищаемые файлы одной карты — нормализованные полные пути (см. NormalizeForMatch).
		struct FProtectedFiles
		{
			FString MapPackage;        // /Game/Map/Login — для лога
			TArray<FString> FileStems; // D:/…/Content/Map/Login, D:/…/Content/Map/Login_BuiltData (файл = основа + «.расширение»)
			TArray<FString> Dirs;      // D:/…/Content/__ExternalActors__/Map/Login, D:/…/Content/__ExternalObjects__/Map/Login
		};

		// Вид совпадения пути с защищаемым набором.
		enum class EMatch : uint8
		{
			None,     // не защищён
			File,     // файл пакета карты или _BuiltData (любое расширение)
			Dir,      // папка внешних акторов/объектов карты или что-то внутри неё
			Ancestor, // папка-родитель защищаемого файла/папки (учитывается только для удаления/перемещения папок)
		};

		// Название совпадения для лога и самотеста.
		const TCHAR* MatchName(EMatch Match)
		{
			switch (Match)
			{
			case EMatch::File: return TEXT("файл");
			case EMatch::Dir: return TEXT("папка OFPA");
			case EMatch::Ancestor: return TEXT("родитель");
			default: return TEXT("нет");
			}
		}

		// Вид нормализованного пути (16.09.2026, ревизия М1).
		enum class EPathKind : uint8
		{
			Full,          // полный: «D:/a/b» или «//сервер/ресурс/a»
			NoDrive,       // «/a/b» без буквы диска: Win32 берёт диск ТЕКУЩЕЙ папки процесса (заранее не известен)
			DriveRelative, // «D:a/b»: относительно текущей папки ДИСКА D (у каждого диска своя — базу заранее не знаем)
		};

		// Нормализованный путь.
		struct FNormPath
		{
			FString Path;                     // Full — полный путь; NoDrive — «/хвост»; DriveRelative — хвост без диска (может быть пустым)
			FString Drive;                    // «D:» у Full с буквой и у DriveRelative
			EPathKind Kind = EPathKind::Full; // вид
		};

		// Локальные псевдонимы: имена этой машины для «\\имя\D$\…» и тома «Volume{GUID}» → «D:».
		struct FLocalAliases
		{
			TArray<FString> Hosts;          // localhost, 127.0.0.1, ::1, [::1], имя компьютера (сравнение без учёта регистра)
			TMap<FString, FString> Volumes; // «volume{guid}» (нижний регистр) → «D:»
		};

		// Собрать псевдонимы (один раз, см. GetLocalAliases).
		FLocalAliases BuildLocalAliases()
		{
			FLocalAliases Out;
			Out.Hosts = { TEXT("localhost"), TEXT("127.0.0.1"), TEXT("::1"), TEXT("[::1]") };
			const FString Computer = FString(FPlatformProcess::ComputerName()).TrimStartAndEnd();
			if (!Computer.IsEmpty())
			{
				Out.Hosts.AddUnique(Computer);
			}
#if PLATFORM_WINDOWS
			// Только несъёмные диски: у пустого съёмного/оптического привода запрос тома бесполезен и может ждать.
			const DWORD Mask = ::GetLogicalDrives();
			for (int32 Index = 0; Index < 26; ++Index)
			{
				if ((Mask & (1u << Index)) == 0)
				{
					continue;
				}
				const TCHAR Root[4] = { TCHAR(TEXT('A') + Index), TEXT(':'), TEXT('\\'), 0 };
				if (::GetDriveTypeW(Root) != DRIVE_FIXED)
				{
					continue;
				}
				TCHAR Name[MAX_PATH] = { 0 };
				if (::GetVolumeNameForVolumeMountPointW(Root, Name, MAX_PATH))
				{
					FString Volume(Name); // «\\?\Volume{GUID}\»
					Volume.ReplaceCharInline(TEXT('\\'), TEXT('/'));
					if (Volume.StartsWith(TEXT("//?/")))
					{
						Volume.RightChopInline(4);
					}
					while (Volume.EndsWith(TEXT("/")))
					{
						Volume.LeftChopInline(1);
					}
					Out.Volumes.Add(Volume.ToLower(), FString::Printf(TEXT("%c:"), TCHAR(TEXT('A') + Index)));
				}
			}
#endif
			return Out;
		}

		// Псевдонимы. Инициализация потокобезопасна (статическая локальная); первый вызов делает Install на игровом потоке.
		// ★ 16.09.2026 (ревизия М5): объект в КУЧЕ и намеренно не освобождается. БЫЛО `static const FLocalAliases Aliases`:
		//   при выгрузке DLL во время работы деструктор статической разрушил бы таблицы, а обёртка (её объект не удаляется,
		//   и фоновый поток мог взять указатель на верхний слой) продолжала бы звать NormalizeForMatch → чтение по мусору.
		const FLocalAliases& GetLocalAliases()
		{
			static const FLocalAliases* Aliases = new FLocalAliases(BuildLocalAliases());
			return *Aliases;
		}

		// Развернуть короткие имена 8.3 («ASDARE~1») в полном пути через GetLongPathNameW. Файла может ещё не быть
		// (создание нового) — тогда разворачивается родитель и дописывается имя. Вызывается только при «~» в пути.
		// ★ 16.09.2026 (ревизия М4): буфер начинается со стековых MAX_PATH и растёт РОВНО до нужного размера (первый вызов
		//   GetLongPathNameW возвращает требуемую длину). БЫЛО: 64 КБ кучи на каждый вызов.
		void ExpandShortNames(FString& Path)
		{
#if PLATFORM_WINDOWS
			const FString Win = Path.Replace(TEXT("/"), TEXT("\\"));
			TArray<TCHAR, TInlineAllocator<MAX_PATH>> Buffer;
			Buffer.SetNumZeroed(MAX_PATH);
			// Развернуть один путь: при нехватке буфера GetLongPathNameW возвращает нужную длину — она и запрашивается.
			auto Expand = [&Buffer](const TCHAR* In, FString& Out)
			{
				DWORD Len = ::GetLongPathNameW(In, Buffer.GetData(), DWORD(Buffer.Num()));
				if (Len >= DWORD(Buffer.Num()) && Len < 65536)
				{
					Buffer.SetNumZeroed(int32(Len) + 1);
					Len = ::GetLongPathNameW(In, Buffer.GetData(), DWORD(Buffer.Num()));
				}
				if (Len == 0 || Len >= DWORD(Buffer.Num()))
				{
					return false;
				}
				Out = FString(Buffer.GetData());
				return true;
			};
			FString Expanded;
			if (Expand(*Win, Expanded))
			{
				Path = Expanded.Replace(TEXT("\\"), TEXT("/"));
				return;
			}
			int32 Slash = INDEX_NONE;
			if (Win.FindLastChar(TEXT('\\'), Slash) && Slash > 2 && Expand(*Win.Left(Slash), Expanded))
			{
				Path = (Expanded + Win.RightChop(Slash)).Replace(TEXT("\\"), TEXT("/"));
			}
#endif
		}

		// Нормализовать путь так, как его поймёт Win32 (сравнение потом без учёта регистра):
		//   - «\» → «/», повторные «/» схлопываются;
		//   - «\\?\», «\\.\», «\??\» (NT-пространство объектов) снимаются; «…\UNC\сервер\…» → «//сервер/…»;
		//     «\\?\Volume{GUID}\…» → «D:/…» по таблице томов несъёмных дисков;
		//   - «\\localhost\D$\…», «\\127.0.0.1\d$\…», «\\<имя компьютера>\D$\…» (административный ресурс) → «D:/…»;
		//   - «/a/b» без буквы — вид NoDrive (диск подставит MatchNormalized); «D:a\b» без «\» после «:» — вид DriveRelative;
		//   - поток NTFS «имя:поток[:$DATA]» пишет в тот же файл → берётся имя до «:»;
		//   - конечные точки и пробелы компонента Win32 отбрасывает («Login.umap.» = «Login.umap») → отбрасываем и мы;
		//   - «.»/«..» схлопываются; относительный путь без диска — от BaseDir (FPaths::ConvertRelativePathToFull, как у
		//     физического слоя Windows: WindowsPlatformFile.cpp:1105-1109);
		//   - короткие имена 8.3 разворачиваются (GetLongPathNameW), если в полном пути есть «~».
		// ★ Не распознаются: символьные ссылки/точки соединения, subst-диски, «\\?\GLOBALROOT\Device\…», сетевые пути к этой
		//   машине через другие имена (FQDN, IP сетевой карты).
		// Потокобезопасна: строки, BaseDir (статическая), неизменяемые псевдонимы, GetLongPathNameW.
		FNormPath NormalizeForMatch(const TCHAR* In)
		{
			const FLocalAliases& Aliases = GetLocalAliases();
			FString Path(In ? In : TEXT(""));
			Path.TrimStartAndEndInline();
			Path.ReplaceCharInline(TEXT('\\'), TEXT('/'));

			// Префиксы Win32/NT.
			if (Path.StartsWith(TEXT("//?/")) || Path.StartsWith(TEXT("//./")) || Path.StartsWith(TEXT("/??/")))
			{
				Path.RightChopInline(4);
				if (Path.StartsWith(TEXT("UNC/"), ESearchCase::IgnoreCase))
				{
					Path = TEXT("//") + Path.RightChop(4);
				}
				else if (Path.StartsWith(TEXT("Volume{"), ESearchCase::IgnoreCase))
				{
					int32 Slash = INDEX_NONE;
					const bool bHasSlash = Path.FindChar(TEXT('/'), Slash);
					if (const FString* Drive = Aliases.Volumes.Find((bHasSlash ? Path.Left(Slash) : Path).ToLower()))
					{
						Path = *Drive + (bHasSlash ? Path.RightChop(Slash) : FString(TEXT("/")));
					}
				}
			}
			// Административный ресурс диска этой машины.
			if (Path.StartsWith(TEXT("//")))
			{
				TArray<FString> UncParts;
				Path.RightChop(2).ParseIntoArray(UncParts, TEXT("/"), /*CullEmpty*/ true);
				const bool bLocalHost = UncParts.Num() >= 2 && Aliases.Hosts.ContainsByPredicate([&UncParts](const FString& Host)
				{
					return Host.Equals(UncParts[0], ESearchCase::IgnoreCase);
				});
				if (bLocalHost && UncParts[1].Len() == 2 && FChar::IsAlpha(UncParts[1][0]) && UncParts[1][1] == TEXT('$'))
				{
					FString Rest;
					for (int32 Index = 2; Index < UncParts.Num(); ++Index)
					{
						Rest += TEXT("/");
						Rest += UncParts[Index];
					}
					Path = FString::Printf(TEXT("%c:"), FChar::ToUpper(UncParts[1][0])) + (Rest.IsEmpty() ? FString(TEXT("/")) : Rest);
				}
			}

			// Вид пути.
			FNormPath Out;
			FString Rest;
			bool bUnc = false;
			const auto HasDrive = [](const FString& S)
			{
				return S.Len() >= 2 && FChar::IsAlpha(S[0]) && S[1] == TEXT(':');
			};
			if (Path.StartsWith(TEXT("//")))
			{
				bUnc = true;
				Rest = Path;
			}
			else if (HasDrive(Path) && (Path.Len() == 2 || Path[2] != TEXT('/')))
			{
				Out.Kind = EPathKind::DriveRelative;
				Out.Drive = Path.Left(2).ToUpper();
				Rest = Path.RightChop(2);
			}
			else if (HasDrive(Path))
			{
				Out.Drive = Path.Left(2).ToUpper();
				Rest = Path.RightChop(2);
			}
			else if (Path.StartsWith(TEXT("/")))
			{
				Out.Kind = EPathKind::NoDrive;
				Rest = Path;
			}
			else
			{
				// Относительный без диска — от BaseDir, как у физического слоя Windows.
				const FString FullPath = FPaths::ConvertRelativePathToFull(Path);
				if (HasDrive(FullPath))
				{
					Out.Drive = FullPath.Left(2).ToUpper();
					Rest = FullPath.RightChop(2);
				}
				else
				{
					bUnc = FullPath.StartsWith(TEXT("//"));
					Out.Kind = bUnc ? EPathKind::Full : EPathKind::NoDrive;
					Rest = FullPath;
				}
			}

			// Компоненты: потоки, конечные точки/пробелы, «.»/«..». У UNC первые два компонента (сервер, ресурс) — как есть.
			TArray<FString> Parts;
			Rest.ParseIntoArray(Parts, TEXT("/"), /*CullEmpty*/ true);
			const int32 MinKeep = bUnc ? FMath::Min(2, Parts.Num()) : 0;
			TArray<FString> Clean;
			Clean.Reserve(Parts.Num());
			for (int32 Index = 0; Index < Parts.Num(); ++Index)
			{
				FString Part = MoveTemp(Parts[Index]);
				if (Index < MinKeep)
				{
					Clean.Add(MoveTemp(Part));
					continue;
				}
				int32 Colon = INDEX_NONE;
				if (Part.FindChar(TEXT(':'), Colon))
				{
					Part.LeftInline(Colon);
				}
				if (Part == TEXT(".."))
				{
					// Выше корня не поднимаемся; у DriveRelative ведущие «..» отбрасываются — базы перебирает MatchNormalized.
					if (Clean.Num() > MinKeep)
					{
						Clean.Pop();
					}
					continue;
				}
				while (Part.Len() > 0 && (Part[Part.Len() - 1] == TEXT('.') || Part[Part.Len() - 1] == TEXT(' ')))
				{
					Part.LeftChopInline(1);
				}
				if (!Part.IsEmpty())
				{
					Clean.Add(MoveTemp(Part));
				}
			}

			const FString Joined = FString::Join(Clean, TEXT("/"));
			switch (Out.Kind)
			{
			case EPathKind::NoDrive:
				Out.Path = TEXT("/") + Joined;
				break;
			case EPathKind::DriveRelative:
				Out.Path = Joined;
				break;
			default:
				Out.Path = bUnc ? TEXT("//") + Joined : (Joined.IsEmpty() ? Out.Drive : Out.Drive + TEXT("/") + Joined);
				if (Out.Path.Contains(TEXT("~")))
				{
					ExpandShortNames(Out.Path);
				}
				break;
			}
			return Out;
		}

		// ★ 16.09.2026 (ревизия М4). БЫСТРЫЙ разбор пути: одна запись в буфер (на стеке), без выделения памяти. Берёт только
		//   простые формы — «D:\путь», «D:/путь» и относительный путь без диска; всё остальное (без буквы диска, «D:хвост»,
		//   «\\?\», «\??\», UNC, тома, короткие имена 8.3, потоки NTFS, конечные точки и пробелы компонента) отдаёт полному
		//   разбору NormalizeForMatch. Результат обязан совпадать с полным — сверяется самотестом (все формы, весь Content).
		//   BaseDirNorm — полный нормализованный BaseDir процесса (считается один раз при установке).
		//   БЫЛО: каждый файловый вызов (в том числе IsReadOnly, который движок зовёт на обходах ассетов) шёл через полный
		//   разбор с десятком временных строк.
		bool TryNormalizeFast(const TCHAR* In, const FString& BaseDirNorm, FStringBuilderBase& Out)
		{
			if (!In || !*In)
			{
				return false;
			}
			if (In[0] == TEXT('/') || In[0] == TEXT('\\') || In[0] == TEXT(' ') || In[0] == TEXT('\t'))
			{
				return false; // без буквы диска, UNC, префиксы NT, путь с ведущими пробелами — полный разбор
			}
			bool bHasDrive = false;
			for (int32 Index = 0; In[Index]; ++Index)
			{
				const TCHAR Char = In[Index];
				if (Char == TEXT('~'))
				{
					return false; // короткое имя 8.3
				}
				if (Char == TEXT(':'))
				{
					if (Index != 1)
					{
						return false; // поток NTFS или что-то ещё
					}
					bHasDrive = true;
				}
			}
			int32 Start = 0;
			Out.Reset();
			if (bHasDrive)
			{
				if (!FChar::IsAlpha(In[0]) || (In[2] != TEXT('/') && In[2] != TEXT('\\')))
				{
					return false; // «D:хвост» относительно текущей папки диска — полный разбор
				}
				Out.AppendChar(FChar::ToUpper(In[0]));
				Out.AppendChar(TEXT(':'));
				Start = 2;
			}
			else
			{
				if (BaseDirNorm.Len() < 2 || BaseDirNorm[1] != TEXT(':'))
				{
					return false; // BaseDir не на диске с буквой
				}
				Out << BaseDirNorm;
			}
			// Смещения начал компонентов — чтобы «..» отрезал последний (в том числе внутри BaseDir).
			TArray<int32, TInlineAllocator<64>> Starts;
			for (int32 Index = 2; Index < Out.Len(); ++Index)
			{
				if (Out.GetData()[Index] == TEXT('/'))
				{
					Starts.Add(Index);
				}
			}
			int32 Index = Start;
			while (In[Index])
			{
				if (In[Index] == TEXT('/') || In[Index] == TEXT('\\'))
				{
					++Index;
					continue;
				}
				int32 End = Index;
				while (In[End] && In[End] != TEXT('/') && In[End] != TEXT('\\'))
				{
					++End;
				}
				const int32 Length = End - Index;
				if (Length == 1 && In[Index] == TEXT('.'))
				{
					Index = End;
					continue;
				}
				if (Length == 2 && In[Index] == TEXT('.') && In[Index + 1] == TEXT('.'))
				{
					if (Starts.Num() > 0)
					{
						const int32 Cut = Starts.Pop();
						Out.RemoveSuffix(Out.Len() - Cut);
					}
					Index = End;
					continue;
				}
				if (In[End - 1] == TEXT('.') || In[End - 1] == TEXT(' '))
				{
					return false; // конечные точки и пробелы Win32 отбрасывает — это делает полный разбор
				}
				Starts.Add(Out.Len());
				Out.AppendChar(TEXT('/'));
				Out.Append(In + Index, Length);
				Index = End;
			}
			return true;
		}

		// Текст нормализованного пути для лога и самотеста.
		FString DescribeNorm(const FNormPath& Norm)
		{
			switch (Norm.Kind)
			{
			case EPathKind::NoDrive:
				return FString::Printf(TEXT("%s (без буквы диска — проверен диск защищаемых путей)"), *Norm.Path);
			case EPathKind::DriveRelative:
				return FString::Printf(TEXT("%s%s (относительно текущей папки диска — проверены все базы)"), *Norm.Drive, *Norm.Path);
			default:
				return Norm.Path;
			}
		}

		// Совпадение нормализованного пути с набором карты. bAncestors — считать ли папки-родители защищаемых путей.
		// ★ 16.09.2026: принимает FStringView — быстрый разбор пути (TryNormalizeFast) отдаёт вид на буфер без выделения памяти.
		EMatch MatchPath(const FProtectedFiles& Set, FStringView Norm, bool bAncestors)
		{
			for (const FString& Stem : Set.FileStems)
			{
				// «<основа>.<расширение>», в хвосте нет «/»: Login.umap, Login.uexp, Login_BuiltData.uasset.
				int32 SlashIndex = INDEX_NONE;
				if (Norm.Len() > Stem.Len() + 1 && Norm[Stem.Len()] == TEXT('.') && Norm.StartsWith(FStringView(Stem), ESearchCase::IgnoreCase)
					&& !Norm.RightChop(Stem.Len()).FindChar(TEXT('/'), SlashIndex))
				{
					return EMatch::File;
				}
			}
			for (const FString& Dir : Set.Dirs)
			{
				if (Norm.Equals(FStringView(Dir), ESearchCase::IgnoreCase)
					|| (Norm.Len() > Dir.Len() && Norm[Dir.Len()] == TEXT('/') && Norm.StartsWith(FStringView(Dir), ESearchCase::IgnoreCase)))
				{
					return EMatch::Dir;
				}
			}
			if (bAncestors)
			{
				auto IsAncestorOf = [&Norm](const FString& Item)
				{
					return Item.Len() > Norm.Len() && Item[Norm.Len()] == TEXT('/')
						&& FStringView(Item).Left(Norm.Len()).Equals(Norm, ESearchCase::IgnoreCase);
				};
				for (const FString& Stem : Set.FileStems)
				{
					if (IsAncestorOf(Stem))
					{
						return EMatch::Ancestor;
					}
				}
				for (const FString& Dir : Set.Dirs)
				{
					if (IsAncestorOf(Dir))
					{
						return EMatch::Ancestor;
					}
				}
			}
			return EMatch::None;
		}

		// Совпадение нормализованного пути ЛЮБОГО вида с набором карты.
		//   Full — прямое сравнение.
		//   NoDrive («/хвост») — диск текущей папки процесса заранее не известен: пробуется диск каждого защищаемого пути.
		//   DriveRelative («D:хвост») — текущая папка диска может быть любой: базой пробуется каждый РОДИТЕЛЬ защищаемого пути.
		//   Совпасть с файлом, папкой OFPA или родителем защищаемого пути «база + хвост» может только при базе — префиксе
		//   защищаемого пути. Консервативно: «D:Login.umap» совпадёт, где бы ни была текущая папка диска D.
		//   ★ База, равная самой папке OFPA или лежащая внутри неё, не перебирается: иначе ЛЮБОЙ «D:хвост» совпал бы как
		//   «папка OFPA». Текущая папка диска внутри __ExternalActors__/…/Login — не рабочее состояние (ограничение, отчёт п. 8).
		EMatch MatchNormalized(const FProtectedFiles& Set, const FNormPath& Norm, bool bAncestors)
		{
			if (Norm.Kind == EPathKind::Full)
			{
				return MatchPath(Set, FStringView(Norm.Path), bAncestors);
			}
			TArray<FString> Items = Set.FileStems;
			Items.Append(Set.Dirs);
			for (const FString& Item : Items)
			{
				if (Item.Len() < 2 || Item[1] != TEXT(':'))
				{
					continue; // защищаемый путь без буквы диска (UNC) — эти виды к нему не применимы
				}
				const FString Drive = Item.Left(2);
				if (Norm.Kind == EPathKind::NoDrive)
				{
					const FString Candidate = Norm.Path == TEXT("/") ? Drive : Drive + Norm.Path;
					const EMatch Match = MatchPath(Set, FStringView(Candidate), bAncestors);
					if (Match != EMatch::None)
					{
						return Match;
					}
					continue;
				}
				if (!Drive.Equals(Norm.Drive, ESearchCase::IgnoreCase))
				{
					continue;
				}
				TArray<FString> Components;
				Item.RightChop(2).ParseIntoArray(Components, TEXT("/"), /*CullEmpty*/ true);
				FString Base = Drive;
				for (int32 Index = 0; Index < Components.Num(); ++Index)
				{
					if (Index > 0)
					{
						Base += TEXT("/");
						Base += Components[Index - 1];
					}
					const FString Candidate = Norm.Path.IsEmpty() ? Base : Base + TEXT("/") + Norm.Path;
					const EMatch Match = MatchPath(Set, FStringView(Candidate), bAncestors);
					if (Match != EMatch::None)
					{
						return Match;
					}
				}
			}
			return EMatch::None;
		}

		// ★ 16.09.2026 (общий список). Правила «пакеты сцены логина» в файловом виде: список пакетов + папка Content.
		struct FAssetRules
		{
			const AsdaLoginPackageList::FMatcher* Matcher = nullptr; // живёт до конца процесса (см. AsdaLoginSaveGuard)
			FString ContentDir;                                      // нормализованный полный путь <Content> без хвостового «/»

			bool IsValid() const { return Matcher != nullptr && !ContentDir.IsEmpty(); }
		};

		// Путь файла под Content → ключ пакета «/game/путь/имя» (нижний регистр). bCutExtension — отбросить расширение
		// последнего сегмента (файл), иначе путь берётся как есть (папка).
		void MakeContentKey(FStringView Relative, bool bCutExtension, FStringBuilderBase& Out)
		{
			Out.Reset();
			Out << TEXT("/game/");
			int32 DotInLast = INDEX_NONE;
			for (int32 Index = 0; Index < Relative.Len(); ++Index)
			{
				const TCHAR Char = Relative[Index];
				if (Char == TEXT('/'))
				{
					DotInLast = INDEX_NONE;
				}
				else if (Char == TEXT('.') && DotInLast == INDEX_NONE)
				{
					DotInLast = Out.Len();
				}
				Out.AppendChar(FChar::ToLower(Char));
			}
			if (bCutExtension && DotInLast != INDEX_NONE)
			{
				Out.RemoveSuffix(Out.Len() - DotInLast);
			}
		}

		// Совпадение ПОЛНОГО нормализованного пути с пакетами логина. File — файл пакета, Dir — сам пакет-папка или путь
		// внутри неё, Ancestor — папка, под которой есть пакеты логина (в том числе папки над Content).
		EMatch MatchAssetPath(const FAssetRules& Rules, FStringView Norm, bool bAncestors)
		{
			if (!Rules.IsValid() || Norm.Len() == 0)
			{
				return EMatch::None;
			}
			const int32 ContentLen = Rules.ContentDir.Len();
			if (Norm.Len() >= ContentLen && Norm.Left(ContentLen).Equals(FStringView(Rules.ContentDir), ESearchCase::IgnoreCase)
				&& (Norm.Len() == ContentLen || Norm[ContentLen] == TEXT('/')))
			{
				if (Norm.Len() == ContentLen)
				{
					// Сама папка Content: при fail-closed она «пакет логина» целиком, иначе — родитель пакетов логина.
					if (Rules.Matcher->ClassifyKey(FStringView(TEXT("/game"))) == AsdaLoginPackageList::EOwnership::Owned)
					{
						return EMatch::Dir;
					}
					return bAncestors && Rules.Matcher->IsAncestorKey(FStringView(TEXT("/game"))) ? EMatch::Ancestor : EMatch::None;
				}
				const FStringView Relative = Norm.RightChop(ContentLen + 1);
				TStringBuilder<256> KeyNoExt;
				MakeContentKey(Relative, /*bCutExtension*/ true, KeyNoExt);
				const AsdaLoginPackageList::EOwnership NoExt = Rules.Matcher->ClassifyKey(KeyNoExt.ToView());
				if (NoExt == AsdaLoginPackageList::EOwnership::Owned)
				{
					// Расширение было отброшено → это файл пакета; иначе путь и так без расширения (папка/пакет).
					return KeyNoExt.Len() < Relative.Len() + 6 ? EMatch::File : EMatch::Dir;
				}
				if (NoExt == AsdaLoginPackageList::EOwnership::Exception)
				{
					return EMatch::None; // общий с игрой пакет внутри папок логина (shared_exceptions)
				}
				TStringBuilder<256> KeyRaw;
				MakeContentKey(Relative, /*bCutExtension*/ false, KeyRaw);
				if (KeyRaw.Len() != KeyNoExt.Len() && Rules.Matcher->ClassifyKey(KeyRaw.ToView()) == AsdaLoginPackageList::EOwnership::Owned)
				{
					return EMatch::Dir; // папка, в имени которой есть точка
				}
				return bAncestors && Rules.Matcher->IsAncestorKey(KeyRaw.ToView()) ? EMatch::Ancestor : EMatch::None;
			}
			// Папка ВЫШЕ Content (диск, папка проекта): её рекурсивное удаление/перемещение задело бы пакеты логина.
			if (bAncestors && Rules.ContentDir.Len() > Norm.Len() && Rules.ContentDir[Norm.Len()] == TEXT('/')
				&& FStringView(Rules.ContentDir).Left(Norm.Len()).Equals(Norm, ESearchCase::IgnoreCase))
			{
				return EMatch::Ancestor;
			}
			return EMatch::None;
		}

		// То же для пути ЛЮБОГО вида (как MatchNormalized у карты): «/хвост» без диска — диск Content; «D:хвост» —
		// базой перебирается каждая папка-родитель пакетов логина (их десятки, не 1529) и папки над Content.
		EMatch MatchAssetNormalized(const FAssetRules& Rules, const FNormPath& Norm, bool bAncestors)
		{
			if (!Rules.IsValid())
			{
				return EMatch::None;
			}
			if (Norm.Kind == EPathKind::Full)
			{
				return MatchAssetPath(Rules, FStringView(Norm.Path), bAncestors);
			}
			if (Rules.ContentDir.Len() < 2 || Rules.ContentDir[1] != TEXT(':'))
			{
				return EMatch::None; // Content не на диске с буквой — эти виды путей к нему не применимы
			}
			const FString Drive = Rules.ContentDir.Left(2);
			if (Norm.Kind == EPathKind::NoDrive)
			{
				const FString Candidate = Norm.Path == TEXT("/") ? Drive : Drive + Norm.Path;
				return MatchAssetPath(Rules, FStringView(Candidate), bAncestors);
			}
			if (!Drive.Equals(Norm.Drive, ESearchCase::IgnoreCase))
			{
				return EMatch::None;
			}
			// Базы: каждая папка над Content, сам Content и каждая папка-родитель пакетов логина внутри Content.
			TArray<FString> Bases;
			{
				const FStringView Content(Rules.ContentDir);
				for (int32 Index = 2; Index < Content.Len(); ++Index)
				{
					if (Content[Index] == TEXT('/'))
					{
						Bases.Add(FString(Content.Left(Index)));
					}
				}
				Bases.Add(Rules.ContentDir);
				for (const FString& Ancestor : Rules.Matcher->GetAncestorDirs())
				{
					// «/game/путь» → «<Content>/путь»; «/game» — сам Content (уже добавлен).
					if (Ancestor.Len() > 5)
					{
						Bases.Add(Rules.ContentDir + Ancestor.RightChop(5));
					}
				}
			}
			for (const FString& Base : Bases)
			{
				const FString Candidate = Norm.Path.IsEmpty() ? Base : Base + TEXT("/") + Norm.Path;
				const EMatch Match = MatchAssetPath(Rules, FStringView(Candidate), bAncestors);
				if (Match != EMatch::None)
				{
					return Match;
				}
			}
			return EMatch::None;
		}

		// Длинное имя пакета/папки → нормализованный полный путь без расширения.
		bool PackageToFullPath(const FString& LongName, FString& OutPath)
		{
			FString Filename;
			if (!FPackageName::TryConvertLongPackageNameToFilename(LongName, Filename, FString()))
			{
				return false;
			}
			const FNormPath Norm = NormalizeForMatch(*Filename);
			OutPath = Norm.Path;
			return Norm.Kind == EPathKind::Full && !OutPath.IsEmpty();
		}

		// Собрать набор файлов карты по длинному имени «/Корень/путь/Имя» (разбор — как в AsdaLoginSaveGuard::MakeProtectedMap).
		bool MakeProtectedFiles(const FString& InPackageName, FProtectedFiles& Out)
		{
			const FString Name = InPackageName.TrimStartAndEnd();
			if (!FPackageName::IsValidLongPackageName(Name))
			{
				return false;
			}
			int32 SecondSlash = INDEX_NONE;
			if (!Name.RightChop(1).FindChar(TEXT('/'), SecondSlash))
			{
				return false;
			}
			SecondSlash += 1;
			const FString Root = Name.Left(SecondSlash);              // /Game
			const FString Relative = Name.RightChop(SecondSlash + 1); // Map/Login

			FString Stem, BuiltStem, ActorsDir, ObjectsDir;
			if (!PackageToFullPath(Name, Stem)
				|| !PackageToFullPath(Name + TEXT("_BuiltData"), BuiltStem)
				|| !PackageToFullPath(Root + TEXT("/") + FPackagePath::GetExternalActorsFolderName() + TEXT("/") + Relative, ActorsDir)
				|| !PackageToFullPath(Root + TEXT("/") + FPackagePath::GetExternalObjectsFolderName() + TEXT("/") + Relative, ObjectsDir))
			{
				return false;
			}
			Out.MapPackage = Name;
			// ★ 16.09.2026 (ревизия §9, МЕЛК-1): пути папок OFPA попадают И В ОСНОВЫ ФАЙЛОВ. Пакет, чьё имя равно пути папки
			//   (/Game/__ExternalActors__/Map/Login → файл Content/__ExternalActors__/Map/Login.uasset), лежит РЯДОМ с папкой,
			//   поэтому правилом «папка/**» не ловился: MatchPath требует «/» после имени папки (Dirs), а основ с таким именем
			//   не было. Порядок первых двух основ важен — строка установки печатает FileStems[0]/[1] как файлы карты.
			// ★ ТРИ МЕСТА, где ловится путь папки OFPA, правятся ВМЕСТЕ (ревизия §10.7): 1) AsdaLoginSaveGuard::FindProtectedIn
			//   (набор карты); 2) Out.FileStems ниже; 3) Out.Dirs ниже. Расхождение любого из них = дыра или лишний отказ.
			Out.FileStems = { Stem, BuiltStem, ActorsDir, ObjectsDir };
			Out.Dirs = { ActorsDir, ObjectsDir };
			return true;
		}

		// Обёртка платформенного файла. Состояние (наборы, тексты) неизменно после конструктора — чтение из любых потоков
		// безопасно; указатель нижнего слоя атомарный (SetLowerLevel может позвать игровой поток при перестройке цепочки).
		class FAsdaLoginGuardPlatformFile final : public IPlatformFile
		{
		public:
			// Наборы карт, список пакетов логина, действующие запреты и тексты для строк отказа.
			FAsdaLoginGuardPlatformFile(TArray<FProtectedFiles>&& InSets, const FAssetRules& InAssetRules, bool bInMapGuard, bool bInAssetGuard,
				const FString& InSessionReason, const FString& InMapHint, const FString& InAssetHint, const FString& InBaseDirNorm)
				: Sets(MoveTemp(InSets))
				, AssetRules(InAssetRules)
				, bMapGuard(bInMapGuard)
				, bAssetGuard(bInAssetGuard)
				, SessionReason(InSessionReason)
				, MapHint(InMapHint)
				, AssetHint(InAssetHint)
				, BaseDirNorm(InBaseDirNorm)
			{
			}

			// Наборы карт (для лога установки и пробы).
			const TArray<FProtectedFiles>& GetSets() const
			{
				return Sets;
			}

			// Правила пакетов логина (для пробы и лога установки).
			const FAssetRules& GetAssetRules() const
			{
				return AssetRules;
			}

			// Действует ли запрет на уровни / на пакеты логина.
			bool IsMapGuardActive() const { return bMapGuard; }
			bool IsAssetGuardActive() const { return bAssetGuard; }

			//~ Цепочка
			virtual bool Initialize(IPlatformFile* Inner, const TCHAR* CmdLine) override
			{
				LowerLevel.store(Inner);
				return Inner != nullptr;
			}
			virtual IPlatformFile* GetLowerLevel() override
			{
				return Lower();
			}
			virtual void SetLowerLevel(IPlatformFile* NewLowerLevel) override
			{
				LowerLevel.store(NewLowerLevel);
			}
			virtual const TCHAR* GetName() const override
			{
				return WrapperName;
			}
			// Tick/InitializeNewAsyncIO/InitializeAfterSetActive не пересылаются: менеджер сам обходит всю цепочку
			// (PlatformFileManager.cpp:66-72, :167-174).
			virtual void SetSandboxEnabled(bool bInEnabled) override { Lower()->SetSandboxEnabled(bInEnabled); }
			virtual bool IsSandboxEnabled() const override { return Lower()->IsSandboxEnabled(); }
			virtual void MakeUniquePakFilesForTheseFiles(const TArray<TArray<FString>>& InFiles) override { Lower()->MakeUniquePakFilesForTheseFiles(InFiles); }

			//~ Чтение и сведения — без проверок
			virtual bool FileExists(const TCHAR* Filename) override { return Lower()->FileExists(Filename); }
			virtual int64 FileSize(const TCHAR* Filename) override { return Lower()->FileSize(Filename); }
			virtual FDateTime GetTimeStamp(const TCHAR* Filename) override { return Lower()->GetTimeStamp(Filename); }
			virtual FDateTime GetAccessTimeStamp(const TCHAR* Filename) override { return Lower()->GetAccessTimeStamp(Filename); }
			virtual FString GetFilenameOnDisk(const TCHAR* Filename) override { return Lower()->GetFilenameOnDisk(Filename); }
			virtual ESymlinkResult IsSymlink(const TCHAR* Filename) override { return Lower()->IsSymlink(Filename); }
			virtual bool HasMarkOfTheWeb(FStringView Filename, FString* OutSourceURL = nullptr) override { return Lower()->HasMarkOfTheWeb(Filename, OutSourceURL); }
			virtual bool SetMarkOfTheWeb(FStringView Filename, bool bNewStatus, const FString* InSourceURL = nullptr) override { return Lower()->SetMarkOfTheWeb(Filename, bNewStatus, InSourceURL); }
			virtual FFileOpenResult OpenRead(const TCHAR* Filename, EOpenReadFlags Flags) override { return Lower()->OpenRead(Filename, Flags); }
			virtual IFileHandle* OpenRead(const TCHAR* Filename, bool bAllowWrite = false) override { return Lower()->OpenRead(Filename, bAllowWrite); }
			virtual FFileOpenResult OpenReadNoBuffering(const TCHAR* Filename, EOpenReadFlags Flags) override { return Lower()->OpenReadNoBuffering(Filename, Flags); }
			virtual IFileHandle* OpenReadNoBuffering(const TCHAR* Filename, bool bAllowWrite = false) override { return Lower()->OpenReadNoBuffering(Filename, bAllowWrite); }
			virtual bool DirectoryExists(const TCHAR* Directory) override { return Lower()->DirectoryExists(Directory); }
			virtual bool CreateDirectory(const TCHAR* Directory) override { return Lower()->CreateDirectory(Directory); }
			virtual bool CreateDirectoryTree(const TCHAR* Directory) override { return Lower()->CreateDirectoryTree(Directory); }
			virtual FFileStatData GetStatData(const TCHAR* FilenameOrDirectory) override { return Lower()->GetStatData(FilenameOrDirectory); }
			virtual bool IterateDirectory(const TCHAR* Directory, FDirectoryVisitor& Visitor) override { return Lower()->IterateDirectory(Directory, Visitor); }
			virtual bool IterateDirectory(const TCHAR* Directory, FDirectoryVisitorFunc Visitor) override { return Lower()->IterateDirectory(Directory, Visitor); }
			virtual bool IterateDirectoryStat(const TCHAR* Directory, FDirectoryStatVisitor& Visitor) override { return Lower()->IterateDirectoryStat(Directory, Visitor); }
			virtual bool IterateDirectoryStat(const TCHAR* Directory, FDirectoryStatVisitorFunc Visitor) override { return Lower()->IterateDirectoryStat(Directory, Visitor); }
			virtual bool IterateDirectoryRecursively(const TCHAR* Directory, FDirectoryVisitor& Visitor) override { return Lower()->IterateDirectoryRecursively(Directory, Visitor); }
			virtual bool IterateDirectoryRecursively(const TCHAR* Directory, FDirectoryVisitorFunc Visitor) override { return Lower()->IterateDirectoryRecursively(Directory, Visitor); }
			virtual bool IterateDirectoryStatRecursively(const TCHAR* Directory, FDirectoryStatVisitor& Visitor) override { return Lower()->IterateDirectoryStatRecursively(Directory, Visitor); }
			virtual bool IterateDirectoryStatRecursively(const TCHAR* Directory, FDirectoryStatVisitorFunc Visitor) override { return Lower()->IterateDirectoryStatRecursively(Directory, Visitor); }
			virtual void FindFiles(TArray<FString>& FoundFiles, const TCHAR* Directory, const TCHAR* FileExtension) override { Lower()->FindFiles(FoundFiles, Directory, FileExtension); }
			virtual void FindFilesRecursively(TArray<FString>& FoundFiles, const TCHAR* Directory, const TCHAR* FileExtension) override { Lower()->FindFilesRecursively(FoundFiles, Directory, FileExtension); }
			virtual FFileOpenAsyncResult OpenAsyncRead(const TCHAR* Filename, EOpenReadFlags Flags) override { return Lower()->OpenAsyncRead(Filename, Flags); }
			virtual IAsyncReadFileHandle* OpenAsyncRead(const TCHAR* Filename, bool bAllowWrite = false) override { return Lower()->OpenAsyncRead(Filename, bAllowWrite); }
			virtual void SetAsyncMinimumPriority(EAsyncIOPriorityAndFlags MinPriority) override { Lower()->SetAsyncMinimumPriority(MinPriority); }
			virtual void GetTimeStampPair(const TCHAR* PathA, const TCHAR* PathB, FDateTime& OutTimeStampA, FDateTime& OutTimeStampB) override { Lower()->GetTimeStampPair(PathA, PathB, OutTimeStampA, OutTimeStampB); }
			virtual FDateTime GetTimeStampLocal(const TCHAR* Filename) override { return Lower()->GetTimeStampLocal(Filename); }
			virtual FString ConvertToAbsolutePathForExternalAppForRead(const TCHAR* Filename) override { return Lower()->ConvertToAbsolutePathForExternalAppForRead(Filename); }
			virtual FString ConvertToAbsolutePathForExternalAppForWrite(const TCHAR* Filename) override { return Lower()->ConvertToAbsolutePathForExternalAppForWrite(Filename); }
			virtual bool SendMessageToServer(const TCHAR* Message, IFileServerMessageHandler* Handler) override { return Lower()->SendMessageToServer(Message, Handler); }
			virtual bool DoesCreatePublicFiles() override { return Lower()->DoesCreatePublicFiles(); }
			virtual void SetCreatePublicFiles(bool bCreatePublicFiles) override { Lower()->SetCreatePublicFiles(bCreatePublicFiles); }
			virtual int64 GetAllowedBytesToWriteThrottledStorage(const TCHAR* DestinationPath = nullptr) override { return Lower()->GetAllowedBytesToWriteThrottledStorage(DestinationPath); }
			virtual bool FileJournalIsAvailable(const TCHAR* VolumeOrPath = nullptr, ELogVerbosity::Type* OutErrorLevel = nullptr, FString* OutError = nullptr) override { return Lower()->FileJournalIsAvailable(VolumeOrPath, OutErrorLevel, OutError); }
			virtual uint64 FileJournalGetMaximumSize(const TCHAR* VolumeOrPath = nullptr, ELogVerbosity::Type* OutErrorLevel = nullptr, FString* OutError = nullptr) const override { return Lower()->FileJournalGetMaximumSize(VolumeOrPath, OutErrorLevel, OutError); }
			virtual EFileJournalResult FileJournalGetLatestEntry(const TCHAR* VolumeName, FFileJournalId& OutJournalId, FFileJournalEntryHandle& OutEntryHandle, FString* OutError = nullptr) override { return Lower()->FileJournalGetLatestEntry(VolumeName, OutJournalId, OutEntryHandle, OutError); }
			virtual bool FileJournalIterateDirectory(const TCHAR* Directory, FDirectoryJournalVisitorFunc Visitor, FString* OutError = nullptr) override { return Lower()->FileJournalIterateDirectory(Directory, Visitor, OutError); }
			virtual FFileJournalData FileJournalGetFileData(const TCHAR* FilenameOrDirectory, FString* OutError = nullptr) override { return Lower()->FileJournalGetFileData(FilenameOrDirectory, OutError); }
			virtual EFileJournalResult FileJournalReadModified(const TCHAR* VolumeName, const FFileJournalId& JournalIdOfStartingEntry, const FFileJournalEntryHandle& StartingJournalEntry,
				TMap<FFileJournalFileHandle, FString>& KnownDirectories, TSet<FString>& OutModifiedDirectories, FFileJournalEntryHandle& OutNextJournalEntry, FString* OutError = nullptr) override
			{
				return Lower()->FileJournalReadModified(VolumeName, JournalIdOfStartingEntry, StartingJournalEntry, KnownDirectories, OutModifiedDirectories, OutNextJournalEntry, OutError);
			}
			virtual FString FileJournalGetVolumeName(FStringView InPath) override { return Lower()->FileJournalGetVolumeName(InPath); }
			// CopyDirectoryTree НЕ переопределён: базовая реализация (GenericPlatformFile.cpp:943) копирует через this->CopyFile,
			// то есть через нашу проверку цели. У физического слоя Windows своей реализации нет.

			//~ Запись/удаление/перемещение — с проверкой
			// ★ 16.09.2026 (ревизия С1б): защищаемый файл отвечает «только для чтения». SaveWorld проверяет это ДО удаления
			//   внешних пакетов карты-назначения (FileHelpers.cpp:1048 «PackageFileIsReadOnly» → ветка с DeleteExistingMapPackages
			//   :1091, которая удаляет через физический слой мимо обёртки). Делегата, способного отменить сохранение раньше, нет:
			//   PreSaveWorldWithContext рассылается из UEditorEngine::Save (EditorEngine.cpp:4720 → :4913) — ПОСЛЕ удаления и
			//   без возможности отказа. Прочие читатели признака в путях записи/удаления/переименования откажутся раньше
			//   (AssetRenameManager.cpp:1255 AutoCheckOut, ObjectTools.cpp:2678/:3363, FileHelpers.cpp:1933/:1992/:4095, AssetTools.cpp:609/:4636).
			//   ★ Признак спрашивают и БЕЗ намерения писать: UncontrolledChangelistsModule.cpp:499 (обход ассетов после первого
			//   сохранения), FHybridPackageStoreBackend (PackageStore.cpp:278, только гибридный cooked-редактор; без готового
			//   пакета откатывается на loose). Поэтому ответ пишется НЕ как Error (иначе любой автоматический прогон со
			//   сохранением получал бы RC=1), а одной строкой Display на путь. Отказ SaveWorld виден по сообщению движка
			//   «PackageFileIsReadOnly» в логе и false у вызывающего.
			virtual bool IsReadOnly(const TCHAR* Filename) override
			{
				FHit Hit;
				if (MatchAny(Filename, /*bAncestors*/ false, Hit))
				{
					bool bFirst = false;
					{
						FScopeLock Lock(&ReadOnlyLogLock);
						if (ReadOnlyLogged.Num() < ReadOnlyLogLimit)
						{
							ReadOnlyLogged.Add(Hit.Norm, &bFirst);
							bFirst = !bFirst; // Add возвращает «уже было» в bIsAlreadyInSet
						}
					}
					if (bFirst)
					{
						UE_LOG(LogAsdaLoginFileGuard, Display, TEXT("[LOGIN-GUARD] IsReadOnly «%s» → «только чтение» (%s, совпадение: %s, полный путь %s; повторные ответы по этому пути не пишутся)"),
							Filename, *Hit.Who, MatchName(Hit.Match), *Hit.Norm);
					}
					return true;
				}
				return Lower()->IsReadOnly(Filename);
			}
			// ★ 16.09.2026 (ревизия С2): на Windows AllowWrite превращается в OpenForWrite (WindowsPlatformFile.cpp:1676-1682):
			//   GENERIC_WRITE, без OpenExisting — CREATE_ALWAYS (:1704-1709), то есть файл обнуляется. Без AllowWrite — только чтение.
			virtual FOpenMappedResult OpenMappedEx(const TCHAR* Filename, EOpenReadFlags OpenOptions = EOpenReadFlags::None, int64 MaximumSize = 0) override
			{
				if (EnumHasAnyFlags(OpenOptions, EOpenReadFlags::AllowWrite) && Refuse(TEXT("OpenMappedEx(AllowWrite)"), Filename, false))
				{
					return MakeError(FString::Printf(TEXT("[LOGIN-GUARD] запись файла уровня логина запрещена: %s"), Filename));
				}
				return Lower()->OpenMappedEx(Filename, OpenOptions, MaximumSize);
			}
			virtual bool DeleteFile(const TCHAR* Filename) override
			{
				return Refuse(TEXT("DeleteFile"), Filename, false) ? false : Lower()->DeleteFile(Filename);
			}
			virtual bool DeleteFiles(const TArrayView<const TCHAR*>& Filenames) override
			{
				TArray<const TCHAR*> Allowed;
				Allowed.Reserve(Filenames.Num());
				bool bRefused = false;
				for (const TCHAR* Filename : Filenames)
				{
					if (Refuse(TEXT("DeleteFiles"), Filename, false))
					{
						bRefused = true;
					}
					else
					{
						Allowed.Add(Filename);
					}
				}
				const bool bLowerOk = Allowed.Num() == 0 || Lower()->DeleteFiles(TArrayView<const TCHAR*>(Allowed));
				return bLowerOk && !bRefused;
			}
			virtual bool MoveFile(const TCHAR* To, const TCHAR* From) override
			{
				// MoveFileW переносит и папки — поэтому родители защищаемых путей тоже в счёт.
				const bool bFrom = Refuse(TEXT("MoveFile (источник)"), From, true);
				const bool bTo = Refuse(TEXT("MoveFile (цель)"), To, true);
				return (bFrom || bTo) ? false : Lower()->MoveFile(To, From);
			}
			virtual bool SetReadOnly(const TCHAR* Filename, bool bNewReadOnlyValue) override
			{
				// Снятие «только чтение» — подготовка к удалению/записи (IFileManager::Delete EvenReadOnly, FILEWRITE_EvenIfReadOnly).
				if (!bNewReadOnlyValue && Refuse(TEXT("SetReadOnly(false)"), Filename, false))
				{
					return false;
				}
				return Lower()->SetReadOnly(Filename, bNewReadOnlyValue);
			}
			virtual void SetTimeStamp(const TCHAR* Filename, FDateTime DateTime) override
			{
				if (!Refuse(TEXT("SetTimeStamp"), Filename, false))
				{
					Lower()->SetTimeStamp(Filename, DateTime);
				}
			}
			virtual IFileHandle* OpenWrite(const TCHAR* Filename, bool bAppend = false, bool bAllowRead = false) override
			{
				return Refuse(TEXT("OpenWrite"), Filename, false) ? nullptr : Lower()->OpenWrite(Filename, bAppend, bAllowRead);
			}
			virtual FFileOpenResult OpenWrite(const TCHAR* Filename, EOpenWriteFlags Flags) override
			{
				if (Refuse(TEXT("OpenWrite"), Filename, false))
				{
					return MakeError(FString::Printf(TEXT("[LOGIN-GUARD] запись файла уровня логина запрещена: %s"), Filename));
				}
				return Lower()->OpenWrite(Filename, Flags);
			}
			virtual FOpenMappedResult OpenMappedEx2(const TCHAR* Filename, EOpenMappedFlags OpenOptions = EOpenMappedFlags::None, int64 MaximumSize = 0) override
			{
				if (EnumHasAnyFlags(OpenOptions, EOpenMappedFlags::OpenForWrite) && Refuse(TEXT("OpenMappedEx2(OpenForWrite)"), Filename, false))
				{
					return MakeError(FString::Printf(TEXT("[LOGIN-GUARD] запись файла уровня логина запрещена: %s"), Filename));
				}
				return Lower()->OpenMappedEx2(Filename, OpenOptions, MaximumSize);
			}
			virtual bool CopyFile(const TCHAR* To, const TCHAR* From, EPlatformFileRead ReadFlags = EPlatformFileRead::None, EPlatformFileWrite WriteFlags = EPlatformFileWrite::None) override
			{
				return Refuse(TEXT("CopyFile (цель)"), To, false) ? false : Lower()->CopyFile(To, From, ReadFlags, WriteFlags);
			}
			virtual bool DeleteDirectory(const TCHAR* Directory) override
			{
				// Без рекурсии удаляется только пустая папка — родители в счёт не идут (иначе ложные отказы на уборке пустых папок).
				return Refuse(TEXT("DeleteDirectory"), Directory, false) ? false : Lower()->DeleteDirectory(Directory);
			}
			virtual bool DeleteDirectoryRecursively(const TCHAR* Directory) override
			{
				// Проверка ДО нижнего слоя: иначе он удалил бы всё, кроме защищаемого, и только потом получил отказ.
				return Refuse(TEXT("DeleteDirectoryRecursively"), Directory, true) ? false : Lower()->DeleteDirectoryRecursively(Directory);
			}

		private:
			// Нижний слой цепочки.
			IPlatformFile* Lower() const
			{
				return LowerLevel.load(std::memory_order_relaxed);
			}

			// Что именно совпало (для строк лога).
			struct FHit
			{
				EMatch Match = EMatch::None; // вид совпадения
				bool bMap = false;           // сработал набор карты (иначе — список пакетов логина)
				FString Who;                 // «карта /Game/Map/Login» или «пакет сцены логина»
				FString Norm;                // нормализованный путь для лога
			};

			// Совпадение пути с ДЕЙСТВУЮЩИМИ наборами: сначала карты (их открывает ASDA_ALLOW_LOGIN_MAP_WRITE), затем пакеты
			// логина из общего списка (ASDA_ALLOW_LOGIN_WRITE). Записи самого уровня в список сторожа не попали, поэтому
			// наборы не перекрываются. Быстрый разбор пути — без выделения памяти, сложные формы уходят в полный.
			bool MatchAny(const TCHAR* Path, bool bAncestors, FHit& OutHit) const
			{
				if (!Path || !*Path || (!bMapGuard && !bAssetGuard))
				{
					return false;
				}
				TStringBuilder<512> Fast;
				if (TryNormalizeFast(Path, BaseDirNorm, Fast))
				{
					const FStringView View = Fast.ToView();
					if (bMapGuard)
					{
						for (const FProtectedFiles& Set : Sets)
						{
							const EMatch Match = MatchPath(Set, View, bAncestors);
							if (Match != EMatch::None)
							{
								OutHit = FHit{ Match, true, FString::Printf(TEXT("карта %s"), *Set.MapPackage), FString(View) };
								return true;
							}
						}
					}
					if (bAssetGuard)
					{
						const EMatch Match = MatchAssetPath(AssetRules, View, bAncestors);
						if (Match != EMatch::None)
						{
							OutHit = FHit{ Match, false, TEXT("пакеты сцены логина"), FString(View) };
							return true;
						}
					}
					return false;
				}
				const FNormPath Norm = NormalizeForMatch(Path);
				if (bMapGuard)
				{
					for (const FProtectedFiles& Set : Sets)
					{
						const EMatch Match = MatchNormalized(Set, Norm, bAncestors);
						if (Match != EMatch::None)
						{
							OutHit = FHit{ Match, true, FString::Printf(TEXT("карта %s"), *Set.MapPackage), DescribeNorm(Norm) };
							return true;
						}
					}
				}
				if (bAssetGuard)
				{
					const EMatch Match = MatchAssetNormalized(AssetRules, Norm, bAncestors);
					if (Match != EMatch::None)
					{
						OutHit = FHit{ Match, false, TEXT("пакеты сцены логина"), DescribeNorm(Norm) };
						return true;
					}
				}
				return false;
			}

			// Защищён ли путь; при защите — строка Error и true.
			bool Refuse(const TCHAR* Operation, const TCHAR* Path, bool bAncestors) const
			{
				FHit Hit;
				if (!MatchAny(Path, bAncestors, Hit))
				{
					return false;
				}
				UE_LOG(LogAsdaLoginFileGuard, Error, TEXT("[LOGIN-GUARD] файловый отказ: %s «%s» (%s, совпадение: %s, полный путь %s; сессия: %s; %s)"),
					Operation, Path, *Hit.Who, MatchName(Hit.Match), *Hit.Norm, *SessionReason, Hit.bMap ? *MapHint : *AssetHint);
				return true;
			}

			// Сколько разных путей пишется в лог ответом «только чтение» (при fail-closed их иначе были бы десятки тысяч).
			static constexpr int32 ReadOnlyLogLimit = 4096;

			TArray<FProtectedFiles> Sets;             // защищаемые карты
			FAssetRules AssetRules;                   // пакеты сцены логина (общий список)
			bool bMapGuard = false;                   // действует запрет на уровни
			bool bAssetGuard = false;                 // действует запрет на пакеты логина
			FString SessionReason;                    // чем опознана автоматическая сессия
			FString MapHint;                          // подсказка про разрешение уровня
			FString AssetHint;                        // подсказка про разрешение пакетов логина
			FString BaseDirNorm;                      // нормализованный BaseDir процесса (для быстрого разбора путей)
			std::atomic<IPlatformFile*> LowerLevel{ nullptr }; // нижний слой
			mutable FCriticalSection ReadOnlyLogLock; // защита ReadOnlyLogged (IsReadOnly зовут из любых потоков)
			TSet<FString> ReadOnlyLogged;             // пути, по которым ответ «только чтение» уже записан в лог
		};

		// Обёртка, стоящая в цепочке (только игровой поток). После снятия объект не удаляется (см. Uninstall).
		FAsdaLoginGuardPlatformFile* ActiveWrapper = nullptr;

		// ★ 16.09.2026 (общий список). Самотест сверки файловых путей с ПАКЕТАМИ ЛОГИНА на синтетическом списке
		//   (настоящие пакеты логина не трогаются): дерево, поддерево-исключение, префикс имени, папки-родители,
		//   формы пути без диска и «D:хвост», а также поведение fail-closed.
		void RunAssetPathSelfTest(int32& InOutPassed, int32& InOutTotal)
		{
			AsdaLoginPackageList::FParsedList List;
			List.Packages.Add({ TEXT("/Game/Map/Login"), AsdaLoginPackageList::EEntryMatch::Tree });
			List.Packages.Add({ TEXT("/Game/Temp/_SelfTestLogin/Tree"), AsdaLoginPackageList::EEntryMatch::Tree });
			List.Packages.Add({ TEXT("/Game/Temp/_SelfTestLogin/Pre"), AsdaLoginPackageList::EEntryMatch::Prefix });
			List.SharedExceptions.Add({ TEXT("/Game/Temp/_SelfTestLogin/Tree/Shared"), AsdaLoginPackageList::EEntryMatch::Exact });
			AsdaLoginPackageList::FMatcher Matcher;
			Matcher.Build(List, /*SubTreeOnly*/ { TEXT("/Game/Map/Login") }, /*DropPaths*/ {});
			Matcher.Finalize();

			FAssetRules Rules;
			Rules.Matcher = &Matcher;
			Rules.ContentDir = NormalizeForMatch(*FPaths::ProjectContentDir()).Path;
			if (Rules.ContentDir.IsEmpty())
			{
				++InOutTotal;
				UE_LOG(LogAsdaLoginFileGuard, Error, TEXT("[LOGIN-GUARD][FILE-SELFTEST] FAIL: не нормализована папка Content"));
				return;
			}
			const FString C = Rules.ContentDir;                  // D:/…/Content
			const FString Drive = C.Left(2);                     // D:
			const FString CNoDrive = C.RightChop(2);             // /Astory/…/Content
			const FString CWin = C.Replace(TEXT("/"), TEXT("\\"));

			struct FCase
			{
				FString Path;
				bool bAncestors;
				EMatch Expect;
			};
			const TArray<FCase> Cases =
			{
				{ C + TEXT("/Temp/_SelfTestLogin/Tree.uasset"), false, EMatch::File },
				{ C + TEXT("/Temp/_SelfTestLogin/Tree.uexp"), false, EMatch::File },
				{ C + TEXT("/Temp/_SelfTestLogin/TREE.UASSET"), false, EMatch::File },
				{ C + TEXT("/Temp/_SelfTestLogin/Tree"), false, EMatch::Dir },
				{ C + TEXT("/Temp/_SelfTestLogin/Tree/Inner.uasset"), false, EMatch::File },
				{ C + TEXT("/Temp/_SelfTestLogin/Tree/Shared.uasset"), false, EMatch::None },      // общий с игрой пакет
				{ C + TEXT("/Temp/_SelfTestLogin/Tree/Shared/Deep.uasset"), false, EMatch::File }, // исключение точечное
				{ C + TEXT("/Temp/_SelfTestLogin/TreeOther.uasset"), false, EMatch::None },
				{ C + TEXT("/Temp/_SelfTestLogin/Pre.uasset"), false, EMatch::File },
				{ C + TEXT("/Temp/_SelfTestLogin/Pre_x_00.uasset"), false, EMatch::File },
				{ C + TEXT("/Temp/_SelfTestLogin/Pre_x_00/Sub.uasset"), false, EMatch::File },
				{ C + TEXT("/Temp/_SelfTestLogin/Qre.uasset"), false, EMatch::None },
				{ C + TEXT("/Temp/_SelfTestLogin"), false, EMatch::None },
				{ C + TEXT("/Temp/_SelfTestLogin"), true, EMatch::Ancestor },
				{ C + TEXT("/Temp"), true, EMatch::Ancestor },
				{ C, true, EMatch::Ancestor },
				{ C, false, EMatch::None },
				{ FPaths::GetPath(C), true, EMatch::Ancestor },   // папка проекта — выше Content
				{ Drive, true, EMatch::Ancestor },
				{ C + TEXT("/Temp/_Other/X.uasset"), true, EMatch::None },
				// Уровень: сам пакет держит НАБОР КАРТЫ (здесь запись превращена в поддерево), папка с тем же именем — список.
				{ C + TEXT("/Map/Login.umap"), false, EMatch::None },
				{ C + TEXT("/Map/Login/Inside.uasset"), false, EMatch::File },
				// Формы пути.
				{ CWin + TEXT("\\Temp\\_SelfTestLogin\\Tree.uasset"), false, EMatch::File },
				{ TEXT("\\??\\") + CWin + TEXT("\\Temp\\_SelfTestLogin\\Tree.uasset"), false, EMatch::File },
				{ CNoDrive + TEXT("/Temp/_SelfTestLogin/Tree.uasset"), false, EMatch::File },
				{ Drive + TEXT("Temp/_SelfTestLogin/Tree.uasset"), false, EMatch::File },          // «D:хвост» от папки Content
				{ C + TEXT("/Temp/_SelfTestLogin/./Tree.uasset"), false, EMatch::File },
				{ C + TEXT("/Temp/_SelfTestLogin/Tree.uasset::$DATA"), false, EMatch::File },
			};
			for (const FCase& Case : Cases)
			{
				const FNormPath Norm = NormalizeForMatch(*Case.Path);
				const EMatch Got = MatchAssetNormalized(Rules, Norm, Case.bAncestors);
				++InOutTotal;
				if (Got == Case.Expect)
				{
					++InOutPassed;
					UE_LOG(LogAsdaLoginFileGuard, Display, TEXT("[LOGIN-GUARD][FILE-SELFTEST] ok (пакеты логина): [%s]%s -> %s"),
						*Case.Path, Case.bAncestors ? TEXT(" +родители") : TEXT(""), MatchName(Got));
				}
				else
				{
					UE_LOG(LogAsdaLoginFileGuard, Error, TEXT("[LOGIN-GUARD][FILE-SELFTEST] FAIL (пакеты логина): [%s]%s -> %s, ожидалось %s (%s)"),
						*Case.Path, Case.bAncestors ? TEXT(" +родители") : TEXT(""), MatchName(Got), MatchName(Case.Expect), *DescribeNorm(Norm));
				}
			}

			// Fail-closed: список не прочитан — всё под Content защищается, вне Content — нет.
			AsdaLoginPackageList::FMatcher Closed;
			Closed.SetFailClosed(TEXT("самотест"));
			Closed.Finalize();
			FAssetRules ClosedRules;
			ClosedRules.Matcher = &Closed;
			ClosedRules.ContentDir = C;
			const TArray<FCase> ClosedCases =
			{
				{ C + TEXT("/Temp/_Other/X.uasset"), false, EMatch::File },
				{ C + TEXT("/Temp/_Other"), false, EMatch::Dir },
				{ C, false, EMatch::Dir },
				{ FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) + TEXT("x.bin"), false, EMatch::None },
				{ FPaths::GetPath(C), true, EMatch::Ancestor },
			};
			for (const FCase& Case : ClosedCases)
			{
				const FNormPath Norm = NormalizeForMatch(*Case.Path);
				const EMatch Got = MatchAssetNormalized(ClosedRules, Norm, Case.bAncestors);
				++InOutTotal;
				if (Got == Case.Expect)
				{
					++InOutPassed;
					UE_LOG(LogAsdaLoginFileGuard, Display, TEXT("[LOGIN-GUARD][FILE-SELFTEST] ok (fail-closed): [%s] -> %s"), *Case.Path, MatchName(Got));
				}
				else
				{
					UE_LOG(LogAsdaLoginFileGuard, Error, TEXT("[LOGIN-GUARD][FILE-SELFTEST] FAIL (fail-closed): [%s] -> %s, ожидалось %s"),
						*Case.Path, MatchName(Got), MatchName(Case.Expect));
				}
			}
		}

		// Самотест разбора путей на наборе Login (не зависит от того, стоит ли обёртка).
		void RunPathSelfTest()
		{
			FProtectedFiles Set;
			if (!MakeProtectedFiles(LoginPackage, Set))
			{
				UE_LOG(LogAsdaLoginFileGuard, Error, TEXT("[LOGIN-GUARD][FILE-SELFTEST] FAIL: не удалось вычислить пути %s"), LoginPackage);
				return;
			}
			const FString C = NormalizeForMatch(*FPaths::ProjectContentDir()).Path;  // D:/…/Content
			const FString CRel = FPaths::ProjectContentDir();                         // ../../../../../…/Content/
			const FString CWin = C.Replace(TEXT("/"), TEXT("\\"));                   // D:\…\Content
			const FString Drive = C.Left(2);                                          // D:
			const FString CNoDrive = C.RightChop(2);                                  // /Astory/AstoryClient/Content
			const FString CWinTail = CWin.RightChop(3);                               // Astory\AstoryClient\Content
			const TCHAR DriveLetter = C[0];                                           // D
			const FString Computer = FPlatformProcess::ComputerName();                // имя этой машины

			struct FCase
			{
				FString Path;     // проверяемый путь
				bool bAncestors;  // учитывать ли родителей
				EMatch Expect;    // ожидаемое совпадение
			};
			TArray<FCase> Cases =
			{
				// ★ 16.09.2026 (ревизия М1): формы путей Win32/NT.
				{ CNoDrive + TEXT("/Map/Login.umap"), false, EMatch::File },
				{ CNoDrive.Replace(TEXT("/"), TEXT("\\")) + TEXT("\\Map\\Login_BuiltData.uasset"), false, EMatch::File },
				{ CNoDrive + TEXT("/Map/LoginX.umap"), false, EMatch::None },
				{ CNoDrive + TEXT("/Map"), true, EMatch::Ancestor },
				{ Drive + TEXT("..\\..\\") + CWinTail + TEXT("\\Map\\Login.umap"), false, EMatch::File },
				{ Drive + TEXT("Login.umap"), false, EMatch::File },
				{ Drive + TEXT("Map/Login_BuiltData.uexp"), false, EMatch::File },
				{ Drive + TEXT("__ExternalActors__\\Map\\Login\\X.uasset"), false, EMatch::Dir },
				{ Drive + TEXT("..\\Content"), true, EMatch::Ancestor },
				{ Drive + TEXT("Other.umap"), false, EMatch::None },
				{ Drive, true, EMatch::Ancestor },
				{ Drive, false, EMatch::None },
				{ TEXT("\\??\\") + CWin + TEXT("\\Map\\Login.umap"), false, EMatch::File },
				{ TEXT("\\??\\UNC\\localhost\\") + FString::Printf(TEXT("%c$\\"), DriveLetter) + CWinTail + TEXT("\\Map\\Login.umap"), false, EMatch::File },
				{ FString::Printf(TEXT("\\\\localhost\\%c$\\"), DriveLetter) + CWinTail + TEXT("\\Map\\Login.umap"), false, EMatch::File },
				{ FString::Printf(TEXT("\\\\127.0.0.1\\%c$\\"), FChar::ToLower(DriveLetter)) + CWinTail + TEXT("\\Map\\Login_BuiltData.uasset"), false, EMatch::File },
				{ FString::Printf(TEXT("\\\\?\\UNC\\LOCALHOST\\%c$\\"), DriveLetter) + CWinTail + TEXT("\\__ExternalObjects__\\Map\\Login\\Y.uasset"), false, EMatch::Dir },
				{ FString::Printf(TEXT("\\\\%s\\%c$\\"), *Computer, DriveLetter) + CWinTail + TEXT("\\Map\\Login.umap"), false, EMatch::File },
				{ FString::Printf(TEXT("//localhost/%c$/"), DriveLetter) + CWinTail.Replace(TEXT("\\"), TEXT("/")), true, EMatch::Ancestor },
				{ FString::Printf(TEXT("\\\\asda-other-host\\%c$\\"), DriveLetter) + CWinTail + TEXT("\\Map\\Login.umap"), false, EMatch::None },
				{ TEXT("\\\\localhost\\share\\") + CWinTail + TEXT("\\Map\\Login.umap"), false, EMatch::None },
				// Прежние случаи (15.09).
				{ CRel + TEXT("Map/Login.umap"), false, EMatch::File },
				{ (CWin + TEXT("\\MAP\\LOGIN.UMAP")).ToUpper(), false, EMatch::File },
				{ TEXT("\\\\?\\") + CWin + TEXT("\\Map\\Login.umap"), false, EMatch::File },
				{ C + TEXT("/Map/Login.umap."), false, EMatch::File },
				{ C + TEXT("/Map/Login.umap ."), false, EMatch::File },
				{ C + TEXT("/Map/Login.umap::$DATA"), false, EMatch::File },
				{ C + TEXT("/Map/Login_BuiltData.uasset"), false, EMatch::File },
				{ C + TEXT("/Map/Login.uexp"), false, EMatch::File },
				{ C + TEXT("/Map/../Map/./Login.umap"), false, EMatch::File },
				{ C + TEXT("//Map//Login.umap"), false, EMatch::File },
				{ C + TEXT("/Map/Login.umap"), true, EMatch::File },
				{ C + TEXT("/Map/Login_Shine.uasset"), false, EMatch::None },
				{ C + TEXT("/Map/LoginX.umap"), false, EMatch::None },
				{ C + TEXT("/Map/Login"), false, EMatch::None },
				{ C + TEXT("/Map/Login.umap/x"), false, EMatch::None },
				{ C + TEXT("/__ExternalActors__/Map/Login/A/B/X.uasset"), false, EMatch::Dir },
				{ C + TEXT("/__ExternalObjects__/Map/Login"), false, EMatch::Dir },
				{ C + TEXT("/__ExternalActors__/Map/LoginOther/X.uasset"), false, EMatch::None },
				// ★ 16.09.2026 (ревизия §9, МЕЛК-1): файл-пакет РЯДОМ с папкой OFPA — имя файла равно пути папки.
				{ C + TEXT("/__ExternalActors__/Map/Login.uasset"), false, EMatch::File },
				{ C + TEXT("/__ExternalObjects__/Map/Login.umap"), false, EMatch::File },
				{ C + TEXT("/__ExternalActors__/Map/Login.uexp"), false, EMatch::File },
				{ C + TEXT("/__ExternalActors__/Map/LoginOther.uasset"), false, EMatch::None },
				{ C + TEXT("/__ExternalActors__/Map/Login_BuiltData.uasset"), false, EMatch::None },
				{ C + TEXT("/Map"), false, EMatch::None },
				{ C + TEXT("/Map"), true, EMatch::Ancestor },
				{ C, true, EMatch::Ancestor },
				{ C + TEXT("/__ExternalActors__/Map"), true, EMatch::Ancestor },
				{ C + TEXT("/Map/Sub"), true, EMatch::None },
				{ C + TEXT("/Maps"), true, EMatch::None },
				{ C + TEXT("/Temp/_Other.uasset"), false, EMatch::None },
			};

			// Том «\\?\Volume{GUID}\…» — если диск проекта нашёлся в таблице томов.
			bool bVolumeCase = false;
			for (const TPair<FString, FString>& Volume : GetLocalAliases().Volumes)
			{
				if (Volume.Value.Equals(Drive, ESearchCase::IgnoreCase))
				{
					Cases.Add({ TEXT("\\\\?\\") + Volume.Key + TEXT("\\") + CWinTail + TEXT("\\Map\\Login.umap"), false, EMatch::File });
					Cases.Add({ TEXT("\\\\?\\") + Volume.Key + TEXT("\\") + CWinTail + TEXT("\\Map\\Other.umap"), false, EMatch::None });
					bVolumeCase = true;
					break;
				}
			}
			if (!bVolumeCase)
			{
				UE_LOG(LogAsdaLoginFileGuard, Warning, TEXT("[LOGIN-GUARD][FILE-SELFTEST] том диска %s не найден — случаи Volume{GUID} пропущены"), *Drive);
			}
#if PLATFORM_WINDOWS
			// Короткие имена 8.3 — если они включены на диске проекта.
			{
				TArray<TCHAR> Buffer;
				Buffer.SetNumZeroed(32768);
				const DWORD Len = ::GetShortPathNameW(*CWin, Buffer.GetData(), Buffer.Num());
				const FString Short = (Len > 0 && Len < DWORD(Buffer.Num())) ? FString(Buffer.GetData()) : FString();
				if (Short.Contains(TEXT("~")))
				{
					Cases.Add({ Short + TEXT("\\Map\\Login.umap"), false, EMatch::File });
					Cases.Add({ Short + TEXT("\\Map\\Login_BuiltData.uasset"), false, EMatch::File }); // файла нет — развёртка родителя
					Cases.Add({ Short + TEXT("\\Map\\LoginX.umap"), false, EMatch::None });
				}
				else
				{
					UE_LOG(LogAsdaLoginFileGuard, Warning, TEXT("[LOGIN-GUARD][FILE-SELFTEST] короткие имена 8.3 для %s недоступны («%s») — случаи 8.3 пропущены"), *CWin, *Short);
				}
			}
#endif

			int32 Passed = 0;
			int32 Total = 0;
			// ★ 16.09.2026 (ревизия М4): на каждом случае быстрый разбор пути сверяется с полным.
			const FNormPath BaseDir = NormalizeForMatch(FPlatformProcess::BaseDir());
			const FString BaseDirNorm = BaseDir.Kind == EPathKind::Full ? BaseDir.Path : FString();
			for (const FCase& Case : Cases)
			{
				const FNormPath Norm = NormalizeForMatch(*Case.Path);
				const EMatch Got = MatchNormalized(Set, Norm, Case.bAncestors);
				++Total;
				if (Got == Case.Expect)
				{
					++Passed;
					UE_LOG(LogAsdaLoginFileGuard, Display, TEXT("[LOGIN-GUARD][FILE-SELFTEST] ok: [%s]%s -> %s (%s)"),
						*Case.Path, Case.bAncestors ? TEXT(" +родители") : TEXT(""), MatchName(Got), *DescribeNorm(Norm));
				}
				else
				{
					UE_LOG(LogAsdaLoginFileGuard, Error, TEXT("[LOGIN-GUARD][FILE-SELFTEST] FAIL: [%s]%s -> %s, ожидалось %s (%s)"),
						*Case.Path, Case.bAncestors ? TEXT(" +родители") : TEXT(""), MatchName(Got), MatchName(Case.Expect), *DescribeNorm(Norm));
				}
				TStringBuilder<512> Fast;
				if (TryNormalizeFast(*Case.Path, BaseDirNorm, Fast))
				{
					++Total;
					if (Norm.Kind == EPathKind::Full && Fast.ToView().Equals(FStringView(Norm.Path), ESearchCase::IgnoreCase))
					{
						++Passed;
					}
					else
					{
						UE_LOG(LogAsdaLoginFileGuard, Error, TEXT("[LOGIN-GUARD][FILE-SELFTEST] FAIL (быстрый разбор): [%s] -> «%s», полный «%s»"),
							*Case.Path, Fast.ToString(), *DescribeNorm(Norm));
					}
				}
			}
			RunAssetPathSelfTest(Passed, Total);
			UE_LOG(LogAsdaLoginFileGuard, Display, TEXT("[LOGIN-GUARD][FILE-SELFTEST] итог %d/%d"), Passed, Total);
		}

		// Проба прямых файловых операций над стендовой КАРТОЙ (файлы карты и папка внешних акторов).
		// Вызывается только из RunFileProbe, который проверяет ASDA_LOGIN_GUARD_TEST_PACKAGE.
		void ProbeMapPackage(const FString& TestPackage, int32& Passed, int32& Total)
		{
			const FProtectedFiles* Set = ActiveWrapper->GetSets().FindByPredicate([&TestPackage](const FProtectedFiles& S)
			{
				return S.MapPackage.Equals(TestPackage, ESearchCase::IgnoreCase);
			});
			FString TestFile;
			if (!Set || !ActiveWrapper->IsMapGuardActive()
				|| !FPackageName::TryConvertLongPackageNameToFilename(TestPackage, TestFile, FPackageName::GetMapPackageExtension()))
			{
				UE_LOG(LogAsdaLoginFileGuard, Warning, TEXT("[LOGIN-GUARD][PROBE] тестовая карта %s не в наборе обёртки (запрет уровней=%s) — проба не выполняется"),
					*TestPackage, ActiveWrapper->IsMapGuardActive() ? TEXT("есть") : TEXT("нет"));
				return;
			}
			TestFile = FPaths::ConvertRelativePathToFull(TestFile);
			IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
			IFileManager& FM = IFileManager::Get();
			if (&PF != ActiveWrapper || !PF.FileExists(*TestFile))
			{
				UE_LOG(LogAsdaLoginFileGuard, Warning, TEXT("[LOGIN-GUARD][PROBE] верхний слой не наш или нет файла %s — проба не выполняется"), *TestFile);
				return;
			}

			auto Expect = [&Passed, &Total](const TCHAR* What, bool bOk)
			{
				++Total;
				if (bOk)
				{
					++Passed;
					UE_LOG(LogAsdaLoginFileGuard, Display, TEXT("[LOGIN-GUARD][PROBE] ok: %s"), What);
				}
				else
				{
					UE_LOG(LogAsdaLoginFileGuard, Error, TEXT("[LOGIN-GUARD][PROBE] FAIL: %s"), What);
				}
			};

			const FFileStatData Before = PF.GetStatData(*TestFile);
			const FString Stem = FPaths::ChangeExtension(TestFile, FString());   // …/_LoginGuardTest (без расширения)
			const FString ParentDir = FPaths::GetPath(TestFile);
			const FString ActorsDir = Set->Dirs[0];
			const FString ProbeDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("LoginGuardProbe"));
			const FString Src = ProbeDir / TEXT("src.bin");
			TArray<uint8> Payload;
			Payload.Init(0x5A, 256);

			// Контроль: обычный файл вне защиты пишется.
			Expect(TEXT("контроль: запись Saved/LoginGuardProbe/src.bin"), FFileHelper::SaveArrayToFile(Payload, *Src));

			// Запись в защищаемый файл.
			{
				TUniquePtr<FArchive> Ar(FM.CreateFileWriter(*TestFile, FILEWRITE_Append | FILEWRITE_EvenIfReadOnly));
				Expect(TEXT("IFileManager::CreateFileWriter(Append|EvenIfReadOnly) → nullptr"), !Ar.IsValid());
			}
			{
				TUniquePtr<IFileHandle> Handle(PF.OpenWrite(*TestFile, true, true));
				Expect(TEXT("OpenWrite(bAppend) → nullptr"), !Handle.IsValid());
			}
			{
				FFileOpenResult Result = PF.OpenWrite(*TestFile, IPlatformFile::EOpenWriteFlags::Append);
				Expect(TEXT("OpenWrite(EOpenWriteFlags::Append) → ошибка"), Result.HasError());
			}
			{
				FOpenMappedResult Result = PF.OpenMappedEx2(*TestFile, IPlatformFile::EOpenMappedFlags::OpenForWrite | IPlatformFile::EOpenMappedFlags::OpenExisting);
				Expect(TEXT("OpenMappedEx2(OpenForWrite|OpenExisting) → ошибка"), Result.HasError());
			}
			// ★ 16.09.2026 (ревизия С2): OpenMappedEx с AllowWrite — отказ; без него — чтение разрешено.
			{
				FOpenMappedResult Result = PF.OpenMappedEx(*TestFile, IPlatformFile::EOpenReadFlags::AllowWrite);
				Expect(TEXT("OpenMappedEx(AllowWrite) → ошибка"), Result.HasError());
			}
			{
				FOpenMappedResult Result = PF.OpenMappedEx(*TestFile);
				Expect(TEXT("OpenMappedEx(None) → отображение для чтения"), Result.HasValue());
			}
			// ★ 16.09.2026 (ревизия С1б): защищаемый файл «только для чтения», файл вне защиты — как на диске.
			Expect(TEXT("IsReadOnly(тестовый файл) → true"), PF.IsReadOnly(*TestFile));
			Expect(TEXT("контроль: IsReadOnly(Saved/LoginGuardProbe/src.bin) → false"), !PF.IsReadOnly(*Src));
			{
				const FDateTime OldTime = PF.GetTimeStamp(*TestFile);
				PF.SetTimeStamp(*TestFile, FDateTime(2001, 1, 1));
				Expect(TEXT("SetTimeStamp — время файла не изменилось"), PF.GetTimeStamp(*TestFile) == OldTime);
			}
			Expect(TEXT("SetReadOnly(false) → false"), !PF.SetReadOnly(*TestFile, false));
			Expect(TEXT("IFileManager::Copy(цель = тестовый файл, Replace, EvenIfReadOnly) → не COPY_OK"), FM.Copy(*TestFile, *Src, true, true) != COPY_OK);
			Expect(TEXT("CopyFile(цель = <основа>.uasset) → false, файла нет"), !PF.CopyFile(*(Stem + TEXT(".uasset")), *Src) && !PF.FileExists(*(Stem + TEXT(".uasset"))));

			// Перемещение и удаление.
			Expect(TEXT("MoveFile(источник = тестовый файл) → false"), !PF.MoveFile(*(ProbeDir / TEXT("moved.umap")), *TestFile));
			Expect(TEXT("MoveFile(цель = <основа>.uexp) → false"), !PF.MoveFile(*(Stem + TEXT(".uexp")), *Src));
			Expect(TEXT("MoveFile(источник = папка теста, родитель) → false"), !PF.MoveFile(*(ProbeDir / TEXT("moved_dir")), *ParentDir));
			Expect(TEXT("IFileManager::Move(источник = тестовый файл, bDoNotRetryOrError) → false"),
				!FM.Move(*(ProbeDir / TEXT("moved2.umap")), *TestFile, true, true, false, true));
			Expect(TEXT("IFileManager::Delete(EvenReadOnly) → false"), !FM.Delete(*TestFile, false, true, false));
			{
				const TCHAR* Names[] = { *TestFile };
				Expect(TEXT("DeleteFiles([тестовый файл]) → false"), !PF.DeleteFiles(TArrayView<const TCHAR*>(Names, 1)));
			}
			// ★ 16.09.2026 (ревизия М1): те же удаление/перемещение в других формах пути.
			{
				const FString Win = TestFile.Replace(TEXT("/"), TEXT("\\")); // D:\…\_LoginGuardTest.umap
				const FString Form1 = TEXT("\\??\\") + Win;
				const FString Form2 = FString::Printf(TEXT("\\\\localhost\\%c$\\%s"), Win[0], *Win.RightChop(3));
				const FString Form3 = TestFile.RightChop(2);                                                       // «/Astory/…» без диска
				const FString Form4 = Win.Left(2) + TEXT("..\\..\\..\\..\\..\\..\\..\\..\\..\\..\\") + Win.RightChop(3); // «D:..\..\…»
				Expect(*FString::Printf(TEXT("DeleteFile(«%s») → false"), *Form1), !PF.DeleteFile(*Form1));
				Expect(*FString::Printf(TEXT("DeleteFile(«%s») → false"), *Form2), !PF.DeleteFile(*Form2));
				Expect(*FString::Printf(TEXT("DeleteFile(«%s») → false"), *Form3), !PF.DeleteFile(*Form3));
				Expect(*FString::Printf(TEXT("MoveFile(источник «%s») → false"), *Form4), !PF.MoveFile(*(ProbeDir / TEXT("moved3.umap")), *Form4));
			}

			// Папки: внешние акторы карты и родитель.
			// Файл-пустышка готовится стендом заранее (расширение не пакетное — реестр ассетов его не разбирает).
			const FString DummyActor = ActorsDir / TEXT("dummy.bin");
			const bool bDummyExists = PF.FileExists(*DummyActor);
			if (bDummyExists)
			{
				Expect(TEXT("IFileManager::Delete(файл внутри __ExternalActors__) → false"), !FM.Delete(*DummyActor, false, true, false));
			}
			{
				TUniquePtr<FArchive> Ar(FM.CreateFileWriter(*(ActorsDir / TEXT("new_probe.uasset"))));
				Expect(TEXT("CreateFileWriter(новый файл внутри __ExternalActors__) → nullptr"), !Ar.IsValid());
			}
			Expect(TEXT("DeleteDirectory(__ExternalActors__ карты) → false"), !PF.DeleteDirectory(*ActorsDir));
			Expect(TEXT("DeleteDirectoryRecursively(__ExternalActors__ карты) → false"), !PF.DeleteDirectoryRecursively(*ActorsDir));
			Expect(TEXT("DeleteDirectoryRecursively(папка теста, родитель) → false"), !PF.DeleteDirectoryRecursively(*ParentDir));
			Expect(TEXT("IFileManager::DeleteDirectory(Tree, папка теста) → false"), !FM.DeleteDirectory(*ParentDir, false, true));

			// ★ 16.09.2026 (ревизия §9, МЕЛК-1): файл-пакет РЯДОМ с папкой OFPA (его имя равно пути папки).
			{
				// Такой пакет на стенде СУЩЕСТВУЕТ (его создаёт стенд), поэтому проверяется отказ и неизменность файла,
				// а не его отсутствие; цель CopyFile берётся в папке __ExternalObjects__, где файла нет.
				const FString ActorsAsPackage = ActorsDir + TEXT(".uasset");
				const FFileStatData PkgBefore = PF.GetStatData(*ActorsAsPackage);
				TUniquePtr<FArchive> Ar(FM.CreateFileWriter(*ActorsAsPackage));
				Expect(TEXT("CreateFileWriter(<папка __ExternalActors__>.uasset) → nullptr"), !Ar.IsValid());
				Expect(TEXT("DeleteFile(<папка __ExternalActors__>.uasset) → false"), !PF.DeleteFile(*ActorsAsPackage));
				const FFileStatData PkgAfter = PF.GetStatData(*ActorsAsPackage);
				Expect(TEXT("<папка __ExternalActors__>.uasset: размер и время изменения те же"),
					PkgBefore.bIsValid == PkgAfter.bIsValid && PkgBefore.FileSize == PkgAfter.FileSize && PkgBefore.ModificationTime == PkgAfter.ModificationTime);
				Expect(TEXT("CopyFile(цель = <папка __ExternalObjects__>.umap) → false, файла нет"),
					!PF.CopyFile(*(Set->Dirs[1] + TEXT(".umap")), *Src) && !PF.FileExists(*(Set->Dirs[1] + TEXT(".umap"))));
			}

			// Чтение разрешено.
			{
				TArray<uint8> Bytes;
				Expect(TEXT("чтение тестового файла целиком (FFileHelper::LoadFileToArray)"), FFileHelper::LoadFileToArray(Bytes, *TestFile) && Bytes.Num() == Before.FileSize);
			}
			{
				FFileOpenResult Result = PF.OpenRead(*TestFile, IPlatformFile::EOpenReadFlags::AllowWrite);
				Expect(TEXT("OpenRead(EOpenReadFlags) → дескриптор"), Result.HasValue());
			}

			// Контроль вне защиты: копия, перемещение, метка времени, удаление, рекурсивное удаление папки.
			const FString Copy = ProbeDir / TEXT("copy.bin");
			const FString Moved = ProbeDir / TEXT("moved.bin");
			Expect(TEXT("контроль: IFileManager::Copy в Saved"), FM.Copy(*Copy, *Src) == COPY_OK);
			Expect(TEXT("контроль: IFileManager::Move в Saved"), FM.Move(*Moved, *Copy));
			Expect(TEXT("контроль: SetReadOnly(false) в Saved"), PF.SetReadOnly(*Moved, false));
			Expect(TEXT("контроль: IFileManager::Delete в Saved"), FM.Delete(*Moved));
			Expect(TEXT("контроль: DeleteDirectoryRecursively(Saved/LoginGuardProbe)"), PF.DeleteDirectoryRecursively(*ProbeDir) && !PF.DirectoryExists(*ProbeDir));

			// Файл теста не изменился.
			const FFileStatData After = PF.GetStatData(*TestFile);
			Expect(TEXT("тестовый файл: размер и время изменения те же"), After.bIsValid && After.FileSize == Before.FileSize && After.ModificationTime == Before.ModificationTime);
			if (bDummyExists)
			{
				Expect(TEXT("dummy.bin внутри __ExternalActors__ на месте"), PF.FileExists(*DummyActor));
			}
			UE_LOG(LogAsdaLoginFileGuard, Display, TEXT("[LOGIN-GUARD][PROBE] карта %s: %d/%d (файл %s, %lld б)"), *TestPackage, Passed, Total, *TestFile, After.FileSize);
		}

		// ★ 16.09.2026 (общий список). Проба над стендовым ПАКЕТОМ ЛОГИНА (не уровень): те же файловые операции, что и у карты,
		//   но набор путей другой — сам файл пакета, его «соседи» по основе и папка-родитель.
		void ProbeAssetPackage(const FString& TestPackage, int32& Passed, int32& Total)
		{
			if (!ActiveWrapper->IsAssetGuardActive())
			{
				UE_LOG(LogAsdaLoginFileGuard, Warning, TEXT("[LOGIN-GUARD][PROBE] запрет пакетов логина не действует — проба пакета %s не выполняется"), *TestPackage);
				return;
			}
			FString TestFile;
			if (!FPackageName::TryConvertLongPackageNameToFilename(TestPackage, TestFile, FPackageName::GetAssetPackageExtension()))
			{
				UE_LOG(LogAsdaLoginFileGuard, Warning, TEXT("[LOGIN-GUARD][PROBE] не вычислен файл пакета %s — проба не выполняется"), *TestPackage);
				return;
			}
			TestFile = FPaths::ConvertRelativePathToFull(TestFile);
			IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
			IFileManager& FM = IFileManager::Get();
			if (&PF != ActiveWrapper || !PF.FileExists(*TestFile))
			{
				UE_LOG(LogAsdaLoginFileGuard, Warning, TEXT("[LOGIN-GUARD][PROBE] верхний слой не наш или нет файла %s — проба не выполняется"), *TestFile);
				return;
			}
			auto Expect = [&Passed, &Total](const TCHAR* What, bool bOk)
			{
				++Total;
				if (bOk)
				{
					++Passed;
					UE_LOG(LogAsdaLoginFileGuard, Display, TEXT("[LOGIN-GUARD][PROBE] ok: %s"), What);
				}
				else
				{
					UE_LOG(LogAsdaLoginFileGuard, Error, TEXT("[LOGIN-GUARD][PROBE] FAIL: %s"), What);
				}
			};

			const FFileStatData Before = PF.GetStatData(*TestFile);
			const FString Stem = FPaths::ChangeExtension(TestFile, FString());
			const FString ParentDir = FPaths::GetPath(TestFile);
			const FString ProbeDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("LoginGuardProbeAsset"));
			const FString Src = ProbeDir / TEXT("src.bin");
			TArray<uint8> Payload;
			Payload.Init(0x5A, 256);
			Expect(TEXT("контроль: запись Saved/LoginGuardProbeAsset/src.bin"), FFileHelper::SaveArrayToFile(Payload, *Src));

			// Запись.
			{
				TUniquePtr<FArchive> Ar(FM.CreateFileWriter(*TestFile, FILEWRITE_Append | FILEWRITE_EvenIfReadOnly));
				Expect(TEXT("пакет логина: CreateFileWriter(Append|EvenIfReadOnly) → nullptr"), !Ar.IsValid());
			}
			{
				TUniquePtr<IFileHandle> Handle(PF.OpenWrite(*TestFile, true, true));
				Expect(TEXT("пакет логина: OpenWrite(bAppend) → nullptr"), !Handle.IsValid());
			}
			{
				FFileOpenResult Result = PF.OpenWrite(*TestFile, IPlatformFile::EOpenWriteFlags::Append);
				Expect(TEXT("пакет логина: OpenWrite(EOpenWriteFlags::Append) → ошибка"), Result.HasError());
			}
			{
				FOpenMappedResult Result = PF.OpenMappedEx(*TestFile, IPlatformFile::EOpenReadFlags::AllowWrite);
				Expect(TEXT("пакет логина: OpenMappedEx(AllowWrite) → ошибка"), Result.HasError());
			}
			{
				FOpenMappedResult Result = PF.OpenMappedEx(*TestFile);
				Expect(TEXT("пакет логина: OpenMappedEx(None) → отображение для чтения"), Result.HasValue());
			}
			Expect(TEXT("пакет логина: IsReadOnly → true"), PF.IsReadOnly(*TestFile));
			Expect(TEXT("контроль: IsReadOnly(Saved/LoginGuardProbeAsset/src.bin) → false"), !PF.IsReadOnly(*Src));
			{
				const FDateTime OldTime = PF.GetTimeStamp(*TestFile);
				PF.SetTimeStamp(*TestFile, FDateTime(2001, 1, 1));
				Expect(TEXT("пакет логина: SetTimeStamp — время файла не изменилось"), PF.GetTimeStamp(*TestFile) == OldTime);
			}
			Expect(TEXT("пакет логина: SetReadOnly(false) → false"), !PF.SetReadOnly(*TestFile, false));
			Expect(TEXT("пакет логина: Copy(цель = файл пакета) → не COPY_OK"), FM.Copy(*TestFile, *Src, true, true) != COPY_OK);
			Expect(TEXT("пакет логина: CopyFile(цель = <основа>.uexp) → false, файла нет"),
				!PF.CopyFile(*(Stem + TEXT(".uexp")), *Src) && !PF.FileExists(*(Stem + TEXT(".uexp"))));

			// Перемещение и удаление (в том числе другие формы пути).
			Expect(TEXT("пакет логина: MoveFile(источник) → false"), !PF.MoveFile(*(ProbeDir / TEXT("moved.uasset")), *TestFile));
			Expect(TEXT("пакет логина: MoveFile(цель = <основа>.ubulk) → false"), !PF.MoveFile(*(Stem + TEXT(".ubulk")), *Src));
			Expect(TEXT("пакет логина: IFileManager::Move(источник, bDoNotRetryOrError) → false"),
				!FM.Move(*(ProbeDir / TEXT("moved2.uasset")), *TestFile, true, true, false, true));
			Expect(TEXT("пакет логина: IFileManager::Delete(EvenReadOnly) → false"), !FM.Delete(*TestFile, false, true, false));
			{
				const TCHAR* Names[] = { *TestFile };
				Expect(TEXT("пакет логина: DeleteFiles([файл]) → false"), !PF.DeleteFiles(TArrayView<const TCHAR*>(Names, 1)));
			}
			{
				const FString Win = TestFile.Replace(TEXT("/"), TEXT("\\"));
				const FString Form1 = TEXT("\\??\\") + Win;
				const FString Form2 = FString::Printf(TEXT("\\\\localhost\\%c$\\%s"), Win[0], *Win.RightChop(3));
				const FString Form3 = TestFile.RightChop(2);
				const FString Form4 = Win.Left(2) + TEXT("..\\..\\..\\..\\..\\..\\..\\..\\..\\..\\") + Win.RightChop(3);
				Expect(*FString::Printf(TEXT("пакет логина: DeleteFile(«%s») → false"), *Form1), !PF.DeleteFile(*Form1));
				Expect(*FString::Printf(TEXT("пакет логина: DeleteFile(«%s») → false"), *Form2), !PF.DeleteFile(*Form2));
				Expect(*FString::Printf(TEXT("пакет логина: DeleteFile(«%s») → false"), *Form3), !PF.DeleteFile(*Form3));
				Expect(*FString::Printf(TEXT("пакет логина: MoveFile(источник «%s») → false"), *Form4), !PF.MoveFile(*(ProbeDir / TEXT("moved3.uasset")), *Form4));
			}

			// Папка-родитель: рекурсивное удаление задело бы пакет логина.
			Expect(TEXT("пакет логина: DeleteDirectoryRecursively(папка теста) → false"), !PF.DeleteDirectoryRecursively(*ParentDir));
			Expect(TEXT("пакет логина: IFileManager::DeleteDirectory(Tree, папка теста) → false"), !FM.DeleteDirectory(*ParentDir, false, true));

			// Чтение разрешено, файл не изменился, контроль в Saved работает.
			{
				TArray<uint8> Bytes;
				Expect(TEXT("пакет логина: чтение файла целиком"), FFileHelper::LoadFileToArray(Bytes, *TestFile) && Bytes.Num() == Before.FileSize);
			}
			const FString Copy = ProbeDir / TEXT("copy.bin");
			Expect(TEXT("контроль: IFileManager::Copy в Saved"), FM.Copy(*Copy, *Src) == COPY_OK);
			Expect(TEXT("контроль: IFileManager::Delete в Saved"), FM.Delete(*Copy));
			Expect(TEXT("контроль: DeleteDirectoryRecursively(Saved/LoginGuardProbeAsset)"), PF.DeleteDirectoryRecursively(*ProbeDir) && !PF.DirectoryExists(*ProbeDir));
			const FFileStatData After = PF.GetStatData(*TestFile);
			Expect(TEXT("пакет логина: размер и время изменения те же"),
				After.bIsValid && After.FileSize == Before.FileSize && After.ModificationTime == Before.ModificationTime);
			UE_LOG(LogAsdaLoginFileGuard, Display, TEXT("[LOGIN-GUARD][PROBE] пакет логина %s: %d/%d (файл %s, %lld б)"),
				*TestPackage, Passed, Total, *TestFile, After.FileSize);
		}

		// Проба прямых файловых операций над ТЕСТОВЫМИ пакетами. Работает только при установленной обёртке и заданной
		// ASDA_LOGIN_GUARD_TEST_PACKAGE: без обёртки эти операции разрушили бы тестовые файлы.
		void RunFileProbe()
		{
			if (!ActiveWrapper)
			{
				UE_LOG(LogAsdaLoginFileGuard, Warning, TEXT("[LOGIN-GUARD][PROBE] обёртка не установлена — проба не выполняется"));
				return;
			}
			const FString Value = FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_LOGIN_GUARD_TEST_PACKAGE")).TrimStartAndEnd();
			TArray<FTestPackage> Items;
			ParseTestPackages(Value, Items);
			// ★ 16.09.2026 (ревизия М4): проба разрушительна при сбое сторожа — только пакеты под ProbePackagePrefix,
			//   иначе отказ ДО любых операций (Login и любые другие пакеты сюда не проходят).
			const bool bAllowed = Items.Num() > 0 && !Items.ContainsByPredicate([](const FTestPackage& Item)
			{
				return !Item.Package.StartsWith(ProbePackagePrefix, ESearchCase::IgnoreCase) || Item.Package.Contains(TEXT(".."))
					|| Item.Package.Len() <= FCString::Strlen(ProbePackagePrefix) || Item.Package.Equals(LoginPackage, ESearchCase::IgnoreCase);
			});
			if (!bAllowed)
			{
				UE_LOG(LogAsdaLoginFileGuard, Error, TEXT("[LOGIN-GUARD][PROBE] отказ пробы: ASDA_LOGIN_GUARD_TEST_PACKAGE='%s' не под %s — никаких операций не выполнено"),
					*Value, ProbePackagePrefix);
				return;
			}
			int32 Passed = 0;
			int32 Total = 0;
			for (const FTestPackage& Item : Items)
			{
				if (Item.bMap)
				{
					ProbeMapPackage(Item.Package, Passed, Total);
				}
				else
				{
					ProbeAssetPackage(Item.Package, Passed, Total);
				}
			}
			UE_LOG(LogAsdaLoginFileGuard, Display, TEXT("[LOGIN-GUARD][PROBE] итог %d/%d (пакетов %d)"), Passed, Total, Items.Num());
		}

		// Замер файловых операций через IFileManager (запись/сведения/чтение/перемещение/удаление).
		// ★ 16.09.2026 (ревизия §9, СРЕДН-1): каталог замера перенесён ИЗ Saved В Content/Temp/_LoginGuardBench. Путь в Saved
		//   выходил из MatchAssetPath на первом же сравнении с <Content> и горячий путь (разбор ключа пакета + двоичные поиски
		//   по спискам) не покрывал. Внутри Content сверка идёт полностью; сами файлы замера пакетами не являются (.bin),
		//   под пакеты логина не подпадают и удаляются в конце.
		void RunFileBench(const TArray<FString>& Args)
		{
			const int32 Count = FMath::Clamp(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 2000, 1, 100000);
			IFileManager& FM = IFileManager::Get();
			const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Temp/_LoginGuardBench"));
			FM.DeleteDirectory(*Dir, false, true);
			FM.MakeDirectory(*Dir, true);
			TArray<uint8> Payload;
			Payload.Init(0xA5, 4096);
			TArray<uint8> ReadBack;
			double TWrite = 0, TStat = 0, TRead = 0, TMove = 0, TDelete = 0, TReadOnly = 0;
			int32 Failures = 0;
			IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
			// ★ 16.09.2026 (ревизия М4): IsReadOnly меряется отдельно — его движок зовёт на обходах ассетов, и именно в нём
			//   сторож разбирает путь. Файлы замера лежат ВНУТРИ Content (ревизия §9, СРЕДН-1), то есть проходят полную сверку
			//   и с картами, и с пакетами логина — тот же горячий путь, что у обходов ассетов движка.
			const int32 ReadOnlyRepeats = 10;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FString A = Dir / FString::Printf(TEXT("f%d.bin"), Index);
				const FString B = Dir / FString::Printf(TEXT("f%d.moved"), Index);
				double T = FPlatformTime::Seconds();
				Failures += FFileHelper::SaveArrayToFile(Payload, *A) ? 0 : 1;
				double T2 = FPlatformTime::Seconds(); TWrite += T2 - T; T = T2;
				for (int32 Repeat = 0; Repeat < ReadOnlyRepeats; ++Repeat)
				{
					Failures += PF.IsReadOnly(*A) ? 1 : 0;
				}
				T2 = FPlatformTime::Seconds(); TReadOnly += T2 - T; T = T2;
				Failures += (FM.FileExists(*A) && FM.FileSize(*A) == Payload.Num() && FM.GetTimeStamp(*A) != FDateTime::MinValue()) ? 0 : 1;
				T2 = FPlatformTime::Seconds(); TStat += T2 - T; T = T2;
				Failures += FFileHelper::LoadFileToArray(ReadBack, *A) ? 0 : 1;
				T2 = FPlatformTime::Seconds(); TRead += T2 - T; T = T2;
				Failures += FM.Move(*B, *A) ? 0 : 1;
				T2 = FPlatformTime::Seconds(); TMove += T2 - T; T = T2;
				Failures += FM.Delete(*B) ? 0 : 1;
				T2 = FPlatformTime::Seconds(); TDelete += T2 - T;
			}
			FM.DeleteDirectory(*Dir, false, true);
			const double PerOp = 1.0e6 / Count;
			UE_LOG(LogAsdaLoginFileGuard, Display, TEXT("[LOGIN-GUARD][BENCH] обёртка=%s N=%d: запись %.1f мс (%.1f мкс), IsReadOnly×%d %.1f мс (%.2f мкс за вызов), сведения×3 %.1f мс (%.1f мкс), чтение %.1f мс (%.1f мкс), перемещение %.1f мс (%.1f мкс), удаление %.1f мс (%.1f мкс), всего %.1f мс, ошибок %d"),
				ActiveWrapper ? TEXT("есть") : TEXT("нет"), Count,
				TWrite * 1000.0, TWrite * PerOp,
				ReadOnlyRepeats, TReadOnly * 1000.0, TReadOnly * PerOp / ReadOnlyRepeats,
				TStat * 1000.0, TStat * PerOp, TRead * 1000.0, TRead * PerOp,
				TMove * 1000.0, TMove * PerOp, TDelete * 1000.0, TDelete * PerOp,
				(TWrite + TReadOnly + TStat + TRead + TMove + TDelete) * 1000.0, Failures);
		}

		// Консольные команды проверки (см. заголовок).
		FAutoConsoleCommand PathSelfTestCommand(
			TEXT("asda.LoginGuard.SelfTestFilePaths"),
			TEXT("Самотест разбора путей файловой защиты логина (нормализация, регистр, \\\\?\\, потоки NTFS, родители)."),
			FConsoleCommandDelegate::CreateStatic(&RunPathSelfTest));

		FAutoConsoleCommand FileProbeCommand(
			TEXT("asda.LoginGuard.FileProbe"),
			TEXT("Проба прямых файловых операций над тестовым пакетом ASDA_LOGIN_GUARD_TEST_PACKAGE (только при установленной файловой обёртке)."),
			FConsoleCommandDelegate::CreateStatic(&RunFileProbe));

		FAutoConsoleCommand FileBenchCommand(
			TEXT("asda.LoginGuard.FileBench"),
			TEXT("Замер файловых операций (N файлов по 4 КБ в Content/Temp/_LoginGuardBench): asda.LoginGuard.FileBench [N=2000]."),
			FConsoleCommandWithArgsDelegate::CreateStatic(&RunFileBench));
	}

	void Install(const FRules& Rules, const FString& SessionReason, const FString& MapPermissionHint, const FString& AssetPermissionHint)
	{
		check(IsInGameThread());
		if (ActiveWrapper)
		{
			return;
		}
		FPlatformFileManager& Manager = FPlatformFileManager::Get();
		if (Manager.FindPlatformFile(WrapperName))
		{
			UE_LOG(LogAsdaLoginFileGuard, Warning, TEXT("[LOGIN-GUARD] слой %s уже в цепочке — повторно не ставится"), WrapperName);
			return;
		}

		// Псевдонимы (имена машины, тома) — собрать на игровом потоке ДО вставки обёртки, чтобы первая запись из фонового
		// потока не ждала запросов томов.
		const FLocalAliases& Aliases = GetLocalAliases();

		TArray<FProtectedFiles> Sets;
		for (const FString& MapPackage : Rules.MapPackages)
		{
			FProtectedFiles Set;
			if (MakeProtectedFiles(MapPackage, Set))
			{
				Sets.Add(MoveTemp(Set));
			}
			else
			{
				UE_LOG(LogAsdaLoginFileGuard, Error, TEXT("[LOGIN-GUARD] не удалось вычислить файлы карты %s — файловая защита этой карты не действует"), *MapPackage);
			}
		}
		// ★ 16.09.2026 (общий список): правила пакетов логина. Папка Content нормализуется один раз здесь.
		FAssetRules AssetRules;
		if (Rules.bAssetGuard && Rules.AssetMatcher)
		{
			const FNormPath Content = NormalizeForMatch(*FPaths::ProjectContentDir());
			if (Content.Kind == EPathKind::Full && !Content.Path.IsEmpty())
			{
				AssetRules.Matcher = Rules.AssetMatcher;
				AssetRules.ContentDir = Content.Path;
			}
			else
			{
				UE_LOG(LogAsdaLoginFileGuard, Error, TEXT("[LOGIN-GUARD] не удалось нормализовать папку Content — файловая защита пакетов логина не действует"));
			}
		}
		const bool bMapGuard = Rules.bMapGuard && Sets.Num() > 0;
		const bool bAssetGuard = AssetRules.IsValid();
		if (!bMapGuard && !bAssetGuard)
		{
			UE_LOG(LogAsdaLoginFileGuard, Error, TEXT("[LOGIN-GUARD] файловый сторож НЕ установлен: нечего защищать (карт %d, список пакетов %s)"),
				Sets.Num(), Rules.AssetMatcher ? TEXT("есть") : TEXT("нет"));
			return;
		}
		// BaseDir процесса для быстрого разбора относительных путей (UE передаёт их как «../../../…»).
		const FNormPath BaseDir = NormalizeForMatch(FPlatformProcess::BaseDir());
		const FString BaseDirNorm = BaseDir.Kind == EPathKind::Full ? BaseDir.Path : FString();

		// ★ Объект не удаляется никогда (в том числе после Uninstall): поток async-IO/сохранения мог получить ссылку на
		//   верхний слой до снятия. DLL модуля при выходе не выгружается (ModuleManager.cpp:1370-1379, «abandon it in memory»).
		FAsdaLoginGuardPlatformFile* Wrapper = new FAsdaLoginGuardPlatformFile(MoveTemp(Sets), AssetRules, bMapGuard, bAssetGuard,
			SessionReason, MapPermissionHint, AssetPermissionHint, BaseDirNorm);
		verify(Wrapper->Initialize(&Manager.GetPlatformFile(), FCommandLine::Get()));
		// Запись указателя верхнего слоя: состояние обёртки и нижний слой готовы ДО неё, поэтому поток, прочитавший старый
		// указатель, работает с прежним (живым) слоем, а прочитавший новый — с полностью готовой обёрткой.
		Manager.SetPlatformFile(*Wrapper);
		ActiveWrapper = Wrapper;

		TArray<FString> Chain;
		for (IPlatformFile* Element = &Manager.GetPlatformFile(); Element; Element = Element->GetLowerLevel())
		{
			Chain.Add(Element->GetName());
		}
		TArray<FString> Paths;
		for (const FProtectedFiles& Set : Wrapper->GetSets())
		{
			// «папка.*|/**» — и файл-пакет с именем папки OFPA (ревизия §9, МЕЛК-1), и всё внутри неё.
			Paths.Add(FString::Printf(TEXT("%s: %s.*, %s.*, %s.*|/**, %s.*|/**"), *Set.MapPackage, *Set.FileStems[0], *Set.FileStems[1], *Set.Dirs[0], *Set.Dirs[1]));
		}
		TArray<FString> VolumeNames;
		for (const TPair<FString, FString>& Volume : Aliases.Volumes)
		{
			VolumeNames.Add(Volume.Value + TEXT("=") + Volume.Key);
		}
		UE_LOG(LogAsdaLoginFileGuard, Display, TEXT("[LOGIN-GUARD] файловый сторож установлен: цепочка [%s]; уровни=%s [%s]; пакеты логина=%s (%s, Content %s); BaseDir %s; локальные имена [%s]; тома [%s]"),
			*FString::Join(Chain, TEXT(" -> ")),
			bMapGuard ? TEXT("да") : TEXT("нет"), *FString::Join(Paths, TEXT("; ")),
			bAssetGuard ? TEXT("да") : TEXT("нет"),
			bAssetGuard ? *AssetRules.Matcher->Describe() : TEXT("список не подключён"),
			bAssetGuard ? *AssetRules.ContentDir : TEXT("-"),
			BaseDirNorm.IsEmpty() ? TEXT("<не разобран, быстрый разбор относительных путей выключен>") : *BaseDirNorm,
			*FString::Join(Aliases.Hosts, TEXT(", ")), *FString::Join(VolumeNames, TEXT(", ")));
	}

	void Uninstall()
	{
		if (!ActiveWrapper)
		{
			return;
		}
		FPlatformFileManager& Manager = FPlatformFileManager::Get();
		// Снять из любого места цепочки (RemovePlatformFile перешивает вышестоящий слой), как PakFileModule::ShutdownModule.
		if (Manager.FindPlatformFile(WrapperName) == ActiveWrapper)
		{
			Manager.RemovePlatformFile(ActiveWrapper);
		}
		ActiveWrapper = nullptr;
		UE_LOG(LogAsdaLoginFileGuard, Display, TEXT("[LOGIN-GUARD] файловый сторож снят с цепочки"));
	}

	bool IsInstalled()
	{
		return ActiveWrapper != nullptr;
	}

	void ParseTestPackages(const FString& Value, TArray<FTestPackage>& Out)
	{
		TArray<FString> Items;
		Value.ParseIntoArray(Items, TEXT(";"), /*CullEmpty*/ true);
		for (FString Item : Items)
		{
			Item.TrimStartAndEndInline();
			Item.TrimQuotesInline();
			Item.TrimStartAndEndInline();
			if (Item.IsEmpty())
			{
				continue;
			}
			FTestPackage Entry;
			if (Item.StartsWith(TEXT("map:"), ESearchCase::IgnoreCase))
			{
				Entry.Package = Item.RightChop(4).TrimStartAndEnd();
				Entry.bMap = true;
			}
			else if (Item.StartsWith(TEXT("asset:"), ESearchCase::IgnoreCase))
			{
				Entry.Package = Item.RightChop(6).TrimStartAndEnd();
				Entry.bMap = false;
			}
			else
			{
				// Без приставки: уровень, если на диске есть файл карты с таким именем (иначе — пакет логина).
				Entry.Package = Item;
				FString MapFile;
				Entry.bMap = FPackageName::TryConvertLongPackageNameToFilename(Item, MapFile, FPackageName::GetMapPackageExtension())
					&& FPlatformFileManager::Get().GetPlatformFile().FileExists(*MapFile);
			}
			if (!Entry.Package.IsEmpty())
			{
				Out.Add(Entry);
			}
		}
	}

	void ForEachContentPackage(const FRules& Rules, TFunctionRef<void(const FString& PackagePath, bool bFileProtected, bool bFastMatchesSlow)> Visitor)
	{
		TArray<FProtectedFiles> Sets;
		for (const FString& MapPackage : Rules.MapPackages)
		{
			FProtectedFiles Set;
			if (MakeProtectedFiles(MapPackage, Set))
			{
				Sets.Add(MoveTemp(Set));
			}
		}
		FAssetRules AssetRules;
		const FNormPath Content = NormalizeForMatch(*FPaths::ProjectContentDir());
		AssetRules.Matcher = Rules.AssetMatcher;
		AssetRules.ContentDir = Content.Path;
		const FNormPath BaseDir = NormalizeForMatch(FPlatformProcess::BaseDir());
		const FString BaseDirNorm = BaseDir.Kind == EPathKind::Full ? BaseDir.Path : FString();
		const FString ContentDir = AssetRules.ContentDir;
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

		// Быстрый разбор против полного на одной форме пути (пустой ответ быстрого — не расхождение: он отдал путь полному).
		auto FastEqualsSlow = [&BaseDirNorm](const FString& Form)
		{
			TStringBuilder<512> Fast;
			if (!TryNormalizeFast(*Form, BaseDirNorm, Fast))
			{
				return true;
			}
			const FNormPath Slow = NormalizeForMatch(*Form);
			return Slow.Kind == EPathKind::Full && Fast.ToView().Equals(FStringView(Slow.Path), ESearchCase::IgnoreCase);
		};

		PlatformFile.IterateDirectoryRecursively(*ContentDir, [&](const TCHAR* FilenameOrDirectory, bool bIsDirectory)
		{
			if (bIsDirectory)
			{
				return true;
			}
			const FStringView File(FilenameOrDirectory);
			if (!File.EndsWith(FStringView(TEXT(".uasset")), ESearchCase::IgnoreCase) && !File.EndsWith(FStringView(TEXT(".umap")), ESearchCase::IgnoreCase))
			{
				return true;
			}
			const FNormPath Norm = NormalizeForMatch(FilenameOrDirectory);
			if (Norm.Kind != EPathKind::Full || Norm.Path.Len() <= ContentDir.Len() + 1)
			{
				return true;
			}
			// Путь пакета: <Content>/путь/Имя.расширение → /Game/путь/Имя (регистр сохраняется).
			FString Relative = Norm.Path.RightChop(ContentDir.Len() + 1);
			int32 Dot = INDEX_NONE;
			if (Relative.FindLastChar(TEXT('.'), Dot))
			{
				Relative.LeftInline(Dot);
			}
			const FString PackagePath = TEXT("/Game/") + Relative;

			bool bProtected = false;
			for (const FProtectedFiles& Set : Sets)
			{
				if (MatchPath(Set, FStringView(Norm.Path), /*bAncestors*/ false) != EMatch::None)
				{
					bProtected = true;
					break;
				}
			}
			if (!bProtected && AssetRules.IsValid())
			{
				bProtected = MatchAssetPath(AssetRules, FStringView(Norm.Path), /*bAncestors*/ false) != EMatch::None;
			}

			// Три формы одного пути: прямые слэши, обратные слэши и «…/Content/./путь».
			const FString WithDot = ContentDir + TEXT("/./") + Norm.Path.RightChop(ContentDir.Len() + 1);
			const bool bFastOk = FastEqualsSlow(Norm.Path) && FastEqualsSlow(Norm.Path.Replace(TEXT("/"), TEXT("\\"))) && FastEqualsSlow(WithDot);
			Visitor(PackagePath, bProtected, bFastOk);
			return true;
		});
	}

	bool PathTargetsMaps(const TArray<FString>& MapPackageNames, const FString& Path, bool bAncestors, FString& OutMap)
	{
		const FNormPath Norm = NormalizeForMatch(*Path);
		for (const FString& MapPackage : MapPackageNames)
		{
			FProtectedFiles Set;
			if (MakeProtectedFiles(MapPackage, Set) && MatchNormalized(Set, Norm, bAncestors) != EMatch::None)
			{
				OutMap = Set.MapPackage;
				return true;
			}
		}
		return false;
	}
}

#endif // WITH_EDITOR
