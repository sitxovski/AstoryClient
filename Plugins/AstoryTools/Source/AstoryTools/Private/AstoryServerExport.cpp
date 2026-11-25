// Copyright (c) 2026 @sitxovski (Discord)
// Реализация общей точки выгрузки файлов для сервера — см. AstoryServerExport.h.
#include "AstoryServerExport.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Misc/SecureHash.h"

DEFINE_LOG_CATEGORY_STATIC(LogAstoryExport, Log, All);

// ─────────────────────────────────────────────────────────────────────────────
// Корни серверов
// ─────────────────────────────────────────────────────────────────────────────
const TArray<FString>& FAstoryServerExport::GetServerRoots()
{
	// ★ Порядок важен: первым идёт ЖИВОЙ сервер (авторитет данных), дальше локальные копии,
	//   которыми пользуемся, когда шара недоступна. Писать сюда нельзя ни при каких условиях —
	//   код открывает файлы только на чтение.
	static TArray<FString> Roots = []()
	{
		TArray<FString> R;
		R.Add(TEXT("//@@ASTORY_SERVER_HOSTNAME@@/h(@@ASTORY_SERVER_HOSTNAME@@_ns)/ASDA/Working_ASDA/v2"));
		R.Add(TEXT("@@ASTORY_DEV_ROOT_FWD@@/Donors"));   // стоковые серверы (GameServer_stock и т.п.)
		R.Add(TEXT("@@ASTORY_DEV_ROOT_FWD@@"));
		return R;
	}();
	return Roots;
}

FString FAstoryServerExport::HashFile(const FString& FullPath)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *FullPath))
	{
		return FString();
	}
	FMD5 Md5;
	Md5.Update(Bytes.GetData(), Bytes.Num());
	uint8 Digest[16];
	Md5.Final(Digest);
	return BytesToHex(Digest, 16).ToLower();
}

// Рекурсивный поиск файла по имени с ограничением глубины.
static void FindFileRecursive(const FString& Dir, const FString& FileName, int32 DepthLeft,
                              TArray<FString>& OutFullPaths)
{
	IFileManager& FM = IFileManager::Get();

	// Файл в текущей папке?
	const FString Candidate = Dir / FileName;
	if (FM.FileExists(*Candidate))
	{
		OutFullPaths.Add(Candidate);
	}
	if (DepthLeft <= 0)
	{
		return;
	}

	TArray<FString> SubDirs;
	FM.FindFiles(SubDirs, *(Dir / TEXT("*")), /*Files=*/false, /*Directories=*/true);
	for (const FString& Sub : SubDirs)
	{
		// Логи, дампы и бэкапы серверных таблиц не содержат — не тратим на них время.
		//   Живой сервер лежит на сетевой шаре, каждый лишний листинг там стоит дорого.
		static const TCHAR* SkipDirs[] =
		{
			TEXT("Log"), TEXT("Logs"), TEXT("server_logs"), TEXT("Dump"), TEXT("Dumps"),
			TEXT("Backup"), TEXT("Backups"), TEXT("Temp"), TEXT("Bin"), TEXT("Debug")
		};
		bool bSkip = Sub.StartsWith(TEXT("."), ESearchCase::CaseSensitive);
		for (const TCHAR* S : SkipDirs)
		{
			if (Sub.Equals(S, ESearchCase::IgnoreCase)) { bSkip = true; break; }
		}
		if (bSkip)
		{
			continue;
		}
		FindFileRecursive(Dir / Sub, FileName, DepthLeft - 1, OutFullPaths);
	}
}

TArray<FAstoryServerPlacement> FAstoryServerExport::DiscoverPlacements(
	const FString& FileName, const FString& BaselineHash, int32 MaxDepth)
{
	TArray<FAstoryServerPlacement> Result;
	IFileManager& FM = IFileManager::Get();

	for (const FString& Root : GetServerRoots())
	{
		if (!FM.DirectoryExists(*Root))
		{
			continue;
		}

		// Типы серверов = папки верхнего уровня корня («GameServer», «NpcServer», …).
		TArray<FString> ServerDirs;
		FM.FindFiles(ServerDirs, *(Root / TEXT("*")), /*Files=*/false, /*Directories=*/true);
		for (const FString& ServerKind : ServerDirs)
		{
			// Отсекаем всё, что заведомо не сервер (в @@ASTORY_DEV_ROOT@@ рядом лежит куча своего).
			if (!ServerKind.Contains(TEXT("Server")) && !ServerKind.Contains(TEXT("Manager")))
			{
				continue;
			}

			TArray<FString> Hits;
			FindFileRecursive(Root / ServerKind, FileName, MaxDepth, Hits);
			for (const FString& Hit : Hits)
			{
				FAstoryServerPlacement P;
				P.ServerKind = ServerKind;
				P.SourceFullPath = Hit;
				P.SourceSize = FM.FileSize(*Hit);
				P.SourceHash = HashFile(Hit);
				P.bSameContent = !BaselineHash.IsEmpty() && P.SourceHash == BaselineHash;

				// Путь внутри сервера без имени файла — он и будет зеркалом в выгрузке.
				FString Rel = Hit;
				Rel.RemoveFromStart(Root / ServerKind / TEXT(""));
				Rel = FPaths::GetPath(Rel);
				P.RelativeDir = Rel;

				if (!P.bSameContent)
				{
					P.Note = FString::Printf(
						TEXT("содержимое ОТЛИЧАЕТСЯ от того, из которого читал редактор (%lld б, md5 %s)"),
						P.SourceSize, *P.SourceHash.Left(8));
				}

				// Дубли между «живой шарой» и локальной копией не нужны: место определяется
				//   парой (тип сервера, путь), а не физическим корнем.
				const bool bDup = Result.ContainsByPredicate([&P](const FAstoryServerPlacement& E)
				{
					return E.ServerKind == P.ServerKind && E.RelativeDir == P.RelativeDir;
				});
				if (!bDup)
				{
					Result.Add(P);
				}
			}
		}

		// Как только нашли хоть что-то в первом доступном корне — дальше не идём: локальные
		//   копии нужны только когда живая шара недоступна.
		if (Result.Num() > 0)
		{
			break;
		}
	}

	Result.StableSort([](const FAstoryServerPlacement& A, const FAstoryServerPlacement& B)
	{
		return A.ServerKind == B.ServerKind ? A.RelativeDir < B.RelativeDir : A.ServerKind < B.ServerKind;
	});
	return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Сборка выгрузки
// ─────────────────────────────────────────────────────────────────────────────
FString FAstoryServerExport::Build(const FAstoryServerExportRequest& Request, FString& OutError)
{
	OutError.Empty();

	if (Request.Files.Num() == 0)
	{
		OutError = TEXT("нечего выгружать: список файлов пуст");
		return FString();
	}

	const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString Base = FPaths::Combine(FPlatformProcess::UserTempDir(),
		FString::Printf(TEXT("AstoryExport_%s"), *Stamp));

	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	if (!PF.CreateDirectoryTree(*Base))
	{
		OutError = FString::Printf(TEXT("не удалось создать папку %s"), *Base);
		return FString();
	}

	FString Readme;
	Readme += TEXT("ВЫГРУЗКА ФАЙЛОВ ДЛЯ СЕРВЕРА\r\n");
	Readme += TEXT("===========================\r\n\r\n");
	Readme += FString::Printf(TEXT("Инструмент : %s\r\n"), *Request.ToolName);
	Readme += FString::Printf(TEXT("Дата       : %s\r\n"), *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S")));
	Readme += TEXT("\r\n");
	Readme += TEXT("ЧТО ИЗМЕНЕНО\r\n------------\r\n");
	Readme += Request.ChangeSummary;
	Readme += TEXT("\r\n\r\n");
	Readme += TEXT("КУДА КОПИРОВАТЬ\r\n---------------\r\n");
	Readme += TEXT("Папки внутри этой выгрузки — ЗЕРКАЛО серверных путей:\r\n");
	Readme += TEXT("  <тип сервера>\\<путь как на сервере>\\<файл>\r\n");
	Readme += TEXT("Копировать в корень сервера, сохраняя структуру.\r\n\r\n");

	int32 Written = 0;
	for (const FAstoryServerExportFile& File : Request.Files)
	{
		if (File.Placements.Num() == 0)
		{
			Readme += FString::Printf(
				TEXT("[!] %s — подходящего места на сервере НЕ НАЙДЕНО, файл не разложен.\r\n"),
				*File.FileName);
			continue;
		}

		if (!File.Note.IsEmpty())
		{
			Readme += FString::Printf(TEXT("\r\n  %s\r\n"), *File.Note);
		}
		for (const FAstoryServerPlacement& P : File.Placements)
		{
			const FString DestDir = File.SubFolder.IsEmpty()
				? FPaths::Combine(Base, P.ServerKind, P.RelativeDir)
				: FPaths::Combine(Base, File.SubFolder, P.ServerKind, P.RelativeDir);
			if (!PF.CreateDirectoryTree(*DestDir))
			{
				OutError = FString::Printf(TEXT("не удалось создать %s"), *DestDir);
				return FString();
			}
			const FString DestFile = FPaths::Combine(DestDir, File.FileName);
			if (!FFileHelper::SaveArrayToFile(File.Content, *DestFile))
			{
				OutError = FString::Printf(TEXT("не удалось записать %s"), *DestFile);
				return FString();
			}
			++Written;
			Readme += File.SubFolder.IsEmpty()
				? FString::Printf(TEXT("  %s\\%s\\%s   (%d б)\r\n"),
					*P.ServerKind, *P.RelativeDir, *File.FileName, File.Content.Num())
				: FString::Printf(TEXT("  %s\\%s\\%s\\%s   (%d б)\r\n"),
					*File.SubFolder, *P.ServerKind, *P.RelativeDir, *File.FileName,
					File.Content.Num());
		}

		// Места с ДРУГИМ поколением файла — только предупреждение, файл туда не кладём.
		bool bHeader = false;
		for (const FAstoryServerPlacement& P : File.AllPlacements)
		{
			if (P.bSameContent)
			{
				continue;
			}
			if (!bHeader)
			{
				Readme += FString::Printf(
					TEXT("\r\n  ⚠ %s найден ЕЩЁ в этих местах, но там ДРУГОЕ содержимое — вслепую не подменять:\r\n"),
					*File.FileName);
				bHeader = true;
			}
			Readme += FString::Printf(TEXT("      %s\\%s\\%s — %s\r\n"),
				*P.ServerKind, *P.RelativeDir, *File.FileName, *P.Note);
		}
		Readme += TEXT("\r\n");
	}

	Readme += TEXT("\r\nКАК ОТКАТИТЬ\r\n------------\r\n");
	Readme += Request.RollbackHint;
	Readme += TEXT("\r\n\r\nНа сервере ничего не менялось автоматически: эта папка — только заготовка.\r\n");

	const FString ReadmePath = FPaths::Combine(Base, TEXT("README.txt"));
	// UTF-8 с BOM: README открывают блокнотом на сервере, без BOM кириллица там ломается.
	FFileHelper::SaveStringToFile(Readme, *ReadmePath,
		FFileHelper::EEncodingOptions::ForceUTF8);

	UE_LOG(LogAstoryExport, Log, TEXT("выгрузка готова: %s (файлов разложено: %d)"), *Base, Written);
	return Base;
}

FString FAstoryServerExport::Run(const FAstoryServerExportRequest& Request, FString& OutError)
{
	const FString Path = Build(Request, OutError);
	if (!Path.IsEmpty())
	{
		FPlatformProcess::ExploreFolder(*Path);
	}
	return Path;
}
