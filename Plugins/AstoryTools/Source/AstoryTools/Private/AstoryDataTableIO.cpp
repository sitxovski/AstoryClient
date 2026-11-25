// Copyright (c) 2026 @sitxovski (Discord)
// Реализация общего шага «CSV → DataTable» — см. AstoryDataTableIO.h.
//   Тело перенесено из FAstoryTitleData::Save (шаг 4) и BackupFile без изменения поведения и текста отчёта.
#include "AstoryDataTableIO.h"

#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

FString FAstoryDataTableIO::BackupFile(const FString& Src, const FString& Dir, const FString& Stamp)
{
	IFileManager& FM = IFileManager::Get();
	if (!FM.FileExists(*Src))
	{
		return FString();
	}
	FM.MakeDirectory(*Dir, /*Tree=*/true);
	const FString Dst = Dir / FString::Printf(TEXT("%s.%s%s"),
		*FPaths::GetBaseFilename(Src), *Stamp, *FPaths::GetExtension(Src, /*bIncludeDot=*/true));
	FM.Copy(*Dst, *Src);
	return Dst;
}

bool FAstoryDataTableIO::ReimportFromCsv(const TCHAR* DataTablePath, const FString& DisplayName, const FString& Csv,
	bool bSavePackage, FString& OutLog)
{
	UDataTable* Table = LoadObject<UDataTable>(nullptr, DataTablePath);
	if (!Table)
	{
		return false;
	}

	Table->Modify();
	const TArray<FString> Problems = Table->CreateTableFromCSVString(Csv);
	if (Problems.Num() > 0)
	{
		// Импортёр ругается на неизвестные колонки и битые значения — это надо ВИДЕТЬ,
		//   молча проглатывать нельзя.
		OutLog += FString::Printf(TEXT("★ импорт %s: замечаний %d\n"), *DisplayName, Problems.Num());
		for (int32 i = 0; i < FMath::Min(Problems.Num(), 8); ++i)
		{
			OutLog += FString::Printf(TEXT("    %s\n"), *Problems[i]);
		}
	}
	FString SaveNote = TEXT(" (ассет НЕ сохранялся — режим проверки)");
	if (bSavePackage)
	{
		// ★★★ Пишем пакет НАПРЯМУЮ, а не через `FEditorFileUtils::PromptForCheckoutAndSave`.
		//   Тот в headless-запуске (`-unattended`) молча ничего не сохраняет: он рапортует
		//   успех, .uasset остаётся со старой датой, и в игре правка выглядит как «не доехала».
		//   Ровно этот дефект уже ловили 30.08.2026 (CSV обновлён, таблица нет).
		//   Прямое сохранение даёт ФАКТ: либо файл записан, либо в отчёте стоит ошибка.
		UPackage* Package = Table->GetOutermost();
		Package->MarkPackageDirty();
		const FString FileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		SaveArgs.Error = GError;
		const FSavePackageResultStruct Res =
			UPackage::Save(Package, nullptr, *FileName, SaveArgs);
		const int64 Size = IFileManager::Get().FileSize(*FileName);
		SaveNote = Res.IsSuccessful()
			? FString::Printf(TEXT(" и СОХРАНЁН: %s (%lld б)"), *FileName, Size)
			: FString::Printf(TEXT(" — ★ ПАКЕТ НЕ СОХРАНЁН: %s"), *FileName);
	}
	OutLog += FString::Printf(TEXT("%s переимпортирован%s (%d строк)\n"),
		*DisplayName, *SaveNote, Table->GetRowNames().Num());
	return true;
}
