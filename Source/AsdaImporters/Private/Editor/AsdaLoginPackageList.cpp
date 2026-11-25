// Copyright (c) 2026 @sitxovski (Discord)
// AsdaLoginPackageList — чтение общего JSON списка пакетов логина и быстрая сверка путей (см. заголовок).
#include "Editor/AsdaLoginPackageList.h"

#if WITH_EDITOR

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformMisc.h"   // ★ 16.09.2026: IsAssetWriteAllowedByEnv — чтение ASDA_ALLOW_LOGIN_WRITE
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace AsdaLoginPackageList
{
	namespace
	{
		// Ожидаемые значения служебных полей (те же, что LOGIN_PACKAGES_FORMAT/VERSION в Python).
		const TCHAR* const ExpectedFormat = TEXT("asda_login_packages");
		const double ExpectedVersion = 1.0;

		// Обязательная запись: без неё список считается чужим (та же проверка в Python).
		const TCHAR* const RequiredTreeEntry = TEXT("/game/map/login");

		// Символ в нижний регистр — как везде в стороже (ASCII; Unicode-регистр в путях пакетов не встречается).
		FORCEINLINE TCHAR Lower(TCHAR Char)
		{
			return FChar::ToLower(Char);
		}

		// Сравнение двух готовых ключей (оба уже в нижнем регистре) — один и тот же порядок при сортировке и поиске.
		FORCEINLINE int32 CompareKeys(FStringView A, FStringView B)
		{
			return A.Compare(B, ESearchCase::CaseSensitive);
		}

		// Двоичный поиск ключа в отсортированном массиве.
		bool SortedContains(const TArray<FString>& Sorted, FStringView Key)
		{
			int32 Low = 0;
			int32 High = Sorted.Num();
			while (Low < High)
			{
				const int32 Mid = Low + (High - Low) / 2;
				const int32 Cmp = CompareKeys(FStringView(Sorted[Mid]), Key);
				if (Cmp == 0)
				{
					return true;
				}
				if (Cmp < 0)
				{
					Low = Mid + 1;
				}
				else
				{
					High = Mid;
				}
			}
			return false;
		}

		// Индекс первого элемента, не меньшего Key (нижняя граница).
		int32 SortedLowerBound(const TArray<FString>& Sorted, FStringView Key)
		{
			int32 Low = 0;
			int32 High = Sorted.Num();
			while (Low < High)
			{
				const int32 Mid = Low + (High - Low) / 2;
				if (CompareKeys(FStringView(Sorted[Mid]), Key) < 0)
				{
					Low = Mid + 1;
				}
				else
				{
					High = Mid;
				}
			}
			return Low;
		}

		// Строка в нижний регистр в буфер (без выделения памяти при коротких путях).
		void AppendLower(FStringView In, FStringBuilderBase& Out)
		{
			for (const TCHAR Char : In)
			{
				Out.AppendChar(Lower(Char));
			}
		}

		// Правильная ли это последовательность UTF-8 (Python на неверной падает UnicodeDecodeError — тоже ошибка).
		bool IsValidUtf8(const TArray<uint8>& Bytes, int32 Start)
		{
			int32 Index = Start;
			while (Index < Bytes.Num())
			{
				const uint8 First = Bytes[Index];
				int32 Extra = 0;
				uint32 Code = 0;
				if (First < 0x80)
				{
					++Index;
					continue;
				}
				else if ((First & 0xE0) == 0xC0) { Extra = 1; Code = First & 0x1F; }
				else if ((First & 0xF0) == 0xE0) { Extra = 2; Code = First & 0x0F; }
				else if ((First & 0xF8) == 0xF0) { Extra = 3; Code = First & 0x07; }
				else { return false; }
				if (Index + Extra >= Bytes.Num())
				{
					return false;
				}
				for (int32 Tail = 1; Tail <= Extra; ++Tail)
				{
					const uint8 Byte = Bytes[Index + Tail];
					if ((Byte & 0xC0) != 0x80)
					{
						return false;
					}
					Code = (Code << 6) | (Byte & 0x3F);
				}
				// Избыточные формы, суррогаты и выход за предел Unicode — Python их тоже не принимает.
				if ((Extra == 1 && Code < 0x80) || (Extra == 2 && Code < 0x800) || (Extra == 3 && Code < 0x10000)
					|| (Code >= 0xD800 && Code <= 0xDFFF) || Code > 0x10FFFF)
				{
					return false;
				}
				Index += Extra + 1;
			}
			return true;
		}

		// Группа JSON: имя, куда класть, какие match допустимы.
		struct FGroupSpec
		{
			const TCHAR* Name;
			TArray<FEntry> FParsedList::* Field;
			bool bExceptions;
		};

		// Значение match для группы (как _LIST_MATCH в Python).
		bool ParseMatch(const FString& Text, bool bExceptions, EEntryMatch& Out)
		{
			if (bExceptions)
			{
				if (Text.Equals(TEXT("exact"), ESearchCase::CaseSensitive))
				{
					Out = EEntryMatch::Exact;
					return true;
				}
				return false;
			}
			if (Text.Equals(TEXT("tree"), ESearchCase::CaseSensitive))
			{
				Out = EEntryMatch::Tree;
				return true;
			}
			if (Text.Equals(TEXT("prefix"), ESearchCase::CaseSensitive))
			{
				Out = EEntryMatch::Prefix;
				return true;
			}
			return false;
		}
	}

	void MakeKey(FStringView PackagePath, FStringBuilderBase& OutKey)
	{
		// Формы пути — те же, что у Python (_normalize_package_path, поле «rules» в JSON): ссылка «Класс'…'» и
		// «/Script/Модуль.Класс'…'», «/All/Game/…», «Game/…» без ведущего «/», дисковый «…/Content/….uasset|.umap»,
		// «\» → «/», повторные «//» → «/», хвостовой «/», путь объекта «…/Имя.Имя[:Подобъект]» → пакет.
		FStringView In = PackagePath;
		while (In.Len() > 0 && FChar::IsWhitespace(In[0]))
		{
			In.RemovePrefix(1);
		}
		while (In.Len() > 0 && FChar::IsWhitespace(In[In.Len() - 1]))
		{
			In.RemoveSuffix(1);
		}
		if (In.Len() >= 2 && (In[In.Len() - 1] == TEXT('\'') || In[In.Len() - 1] == TEXT('"')))
		{
			const TCHAR Quote = In[In.Len() - 1];
			int32 Open = INDEX_NONE;
			if (In.FindChar(Quote, Open) && Open < In.Len() - 1)
			{
				In = In.Mid(Open + 1, In.Len() - Open - 2); // Класс'путь' → путь
			}
		}
		// Нижний регистр, «\»→«/», схлопывание повторных «/».
		TStringBuilder<256> Low;
		for (TCHAR Char : In)
		{
			if (Char == TEXT('\\'))
			{
				Char = TEXT('/');
			}
			if (Char == TEXT('/') && Low.Len() > 0 && Low.GetData()[Low.Len() - 1] == TEXT('/'))
			{
				continue;
			}
			Low.AppendChar(Lower(Char));
		}
		FStringView View = Low.ToView();
		while (View.Len() > 0 && View[View.Len() - 1] == TEXT('/'))
		{
			View.RemoveSuffix(1);
		}
		OutKey.Reset();
		// Дисковый путь пакета «…/content/путь/имя.uasset|.umap» → «/game/путь/имя».
		const bool bPackageFile = View.EndsWith(FStringView(TEXT(".uasset")), ESearchCase::CaseSensitive)
			|| View.EndsWith(FStringView(TEXT(".umap")), ESearchCase::CaseSensitive);
		int32 ContentAt = INDEX_NONE;
		if (bPackageFile)
		{
			const FStringView Needle(TEXT("/content/"));
			for (int32 Index = View.Len() - Needle.Len(); Index >= 0; --Index)
			{
				if (View.Mid(Index, Needle.Len()).Equals(Needle, ESearchCase::CaseSensitive))
				{
					ContentAt = Index;
					break;
				}
			}
		}
		if (ContentAt != INDEX_NONE)
		{
			OutKey << TEXT("/game/") << View.RightChop(ContentAt + 9);
		}
		else if (View.StartsWith(FStringView(TEXT("/all/")), ESearchCase::CaseSensitive))
		{
			OutKey << View.RightChop(4);
		}
		else if (View.StartsWith(FStringView(TEXT("game/")), ESearchCase::CaseSensitive))
		{
			OutKey << TEXT("/") << View;
		}
		else
		{
			OutKey << View;
		}
		// «…/Имя.Имя[:Подобъект]» → пакет: всё от первой точки в последнем сегменте отбрасывается (как в Python).
		const FStringView Result = OutKey.ToView();
		int32 LastSlash = INDEX_NONE;
		for (int32 Index = Result.Len() - 1; Index >= 0; --Index)
		{
			if (Result[Index] == TEXT('/'))
			{
				LastSlash = Index;
				break;
			}
		}
		for (int32 Index = LastSlash + 1; Index < Result.Len(); ++Index)
		{
			if (Result[Index] == TEXT('.'))
			{
				OutKey.RemoveSuffix(Result.Len() - Index);
				break;
			}
		}
	}

	FString GetProjectListPath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Tools/data/asda_login_packages.json"));
	}

	bool ParseBytes(const TArray<uint8>& Bytes, FParsedList& Out, FString& OutError)
	{
		Out = FParsedList();
		if (Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF)
		{
			OutError = TEXT("файл начинается с BOM (UTF-8 BOM запрещён: json.load в Python на нём падает)");
			return false;
		}
		if (!IsValidUtf8(Bytes, 0))
		{
			OutError = TEXT("файл не в UTF-8");
			return false;
		}
		FString Text;
		if (Bytes.Num() > 0)
		{
			FFileHelper::BufferToString(Text, Bytes.GetData(), Bytes.Num());
		}
		TSharedPtr<FJsonValue> Root;
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("JSON не разобран: %s"), *Reader->GetErrorMessage());
			return false;
		}
		const TSharedPtr<FJsonObject>* RootObjectPtr = nullptr;
		if (!Root->TryGetObject(RootObjectPtr) || !RootObjectPtr || !RootObjectPtr->IsValid())
		{
			OutError = TEXT("корень JSON — не объект");
			return false;
		}
		const TSharedPtr<FJsonObject>& RootObject = *RootObjectPtr;

		// format/version: те же значения, что ждёт Python (version принимается и числом 1, и 1.0, и true — как «!= 1»).
		const TSharedPtr<FJsonValue> FormatValue = RootObject->TryGetField(TEXT("format"));
		if (!FormatValue.IsValid() || FormatValue->Type != EJson::String || FormatValue->AsString() != ExpectedFormat)
		{
			OutError = FString::Printf(TEXT("format не %s"), ExpectedFormat);
			return false;
		}
		const TSharedPtr<FJsonValue> VersionValue = RootObject->TryGetField(TEXT("version"));
		const bool bVersionOk = VersionValue.IsValid()
			&& ((VersionValue->Type == EJson::Number && VersionValue->AsNumber() == ExpectedVersion)
				|| (VersionValue->Type == EJson::Boolean && VersionValue->AsBool()));
		if (!bVersionOk)
		{
			OutError = FString::Printf(TEXT("version не %d"), int32(ExpectedVersion));
			return false;
		}

		const FGroupSpec Groups[] =
		{
			{ TEXT("model_dirs"), &FParsedList::ModelDirs, false },
			{ TEXT("packages"), &FParsedList::Packages, false },
			{ TEXT("shared_exceptions"), &FParsedList::SharedExceptions, true },
		};
		TSet<FString> Seen;
		for (const FGroupSpec& Group : Groups)
		{
			const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
			if (!RootObject->TryGetArrayField(Group.Name, Items) || !Items)
			{
				OutError = FString::Printf(TEXT("нет массива %s"), Group.Name);
				return false;
			}
			TArray<FEntry>& Target = Out.*Group.Field;
			for (int32 Index = 0; Index < Items->Num(); ++Index)
			{
				const TSharedPtr<FJsonValue>& Item = (*Items)[Index];
				const TSharedPtr<FJsonObject>* ItemObject = nullptr;
				FString Path;
				FString Match;
				const bool bShape = Item.IsValid() && Item->TryGetObject(ItemObject) && ItemObject && ItemObject->IsValid()
					&& (*ItemObject)->TryGetStringField(TEXT("path"), Path)
					&& (*ItemObject)->TryGetStringField(TEXT("match"), Match);
				EEntryMatch EntryMatch = EEntryMatch::Tree;
				if (!bShape
					|| !Path.StartsWith(TEXT("/Game/"), ESearchCase::CaseSensitive)
					|| Path.Contains(TEXT("*"), ESearchCase::CaseSensitive)
					|| Path.Contains(TEXT("\\"), ESearchCase::CaseSensitive)
					|| Path.EndsWith(TEXT("/"), ESearchCase::CaseSensitive)
					|| !ParseMatch(Match, Group.bExceptions, EntryMatch))
				{
					OutError = FString::Printf(TEXT("%s[%d]: неверная запись (path='%s', match='%s')"), Group.Name, Index, *Path, *Match);
					return false;
				}
				TStringBuilder<256> Key;
				Key << (Group.bExceptions ? TEXT("E|") : TEXT("N|"));
				AppendLower(Path, Key);
				Key << TEXT("|") << Match;
				bool bAlready = false;
				Seen.Add(FString(Key.ToView()), &bAlready);
				if (bAlready)
				{
					OutError = FString::Printf(TEXT("%s[%d]: повтор %s"), Group.Name, Index, *Path);
					return false;
				}
				Target.Add(FEntry{ Path, EntryMatch });
			}
		}

		// Обязательная запись уровня.
		bool bHasLogin = false;
		for (const TArray<FEntry>* List : { &Out.ModelDirs, &Out.Packages })
		{
			for (const FEntry& Entry : *List)
			{
				TStringBuilder<128> Key;
				AppendLower(Entry.Path, Key);
				if (Entry.Match == EEntryMatch::Tree && Key.ToView().Equals(FStringView(RequiredTreeEntry), ESearchCase::CaseSensitive))
				{
					bHasLogin = true;
				}
			}
		}
		if (!bHasLogin)
		{
			OutError = TEXT("нет записи {/Game/Map/Login, tree}");
			return false;
		}
		return true;
	}

	bool LoadFile(const FString& Path, FParsedList& Out, FString& OutError)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path))
		{
			Out = FParsedList();
			OutError = TEXT("файл не прочитан");
			return false;
		}
		return ParseBytes(Bytes, Out, OutError);
	}

	void FMatcher::AddAncestors(FStringView LowerPath, bool bIncludeSelf)
	{
		for (int32 Index = 1; Index < LowerPath.Len(); ++Index)
		{
			if (LowerPath[Index] == TEXT('/'))
			{
				AncestorDirs.AddUnique(FString(LowerPath.Left(Index)));
			}
		}
		if (bIncludeSelf)
		{
			AncestorDirs.AddUnique(FString(LowerPath));
		}
	}

	void FMatcher::AddEntry(const FEntry& Entry)
	{
		TStringBuilder<256> Key;
		AppendLower(Entry.Path, Key);
		const FStringView View = Key.ToView();
		++EntryCount;
		switch (Entry.Match)
		{
		case EEntryMatch::Tree:
			Trees.Add(FString(View));
			AddAncestors(View, /*bIncludeSelf*/ false);
			break;
		case EEntryMatch::SubTree:
			SubTrees.Add(FString(View));
			AddAncestors(View, /*bIncludeSelf*/ true); // сама папка — родитель своего содержимого
			break;
		case EEntryMatch::Exact:
			Exceptions.Add(FString(View));
			break;
		case EEntryMatch::Prefix:
		{
			int32 LastSlash = INDEX_NONE;
			for (int32 Index = View.Len() - 1; Index >= 0; --Index)
			{
				if (View[Index] == TEXT('/'))
				{
					LastSlash = Index;
					break;
				}
			}
			if (LastSlash <= 0 || LastSlash + 1 >= View.Len())
			{
				break; // «/имя» без папки — в списке невозможен (проверка path при разборе)
			}
			PrefixParents.Add(FString(View.Left(LastSlash)));
			PrefixNames.Add(FString(View.RightChop(LastSlash + 1)));
			AddAncestors(View.Left(LastSlash), /*bIncludeSelf*/ true);
			break;
		}
		}
	}

	void FMatcher::Build(const FParsedList& Parsed, const TArray<FString>& SubTreeOnly, const TArray<FString>& DropPaths)
	{
		Trees.Reset();
		SubTrees.Reset();
		Exceptions.Reset();
		PrefixParents.Reset();
		PrefixNames.Reset();
		AncestorDirs.Reset();
		bFailClosed = false;
		FailReason.Empty();
		EntryCount = 0;
		for (const TArray<FEntry>* List : { &Parsed.ModelDirs, &Parsed.Packages, &Parsed.SharedExceptions })
		{
			for (const FEntry& Entry : *List)
			{
				const bool bDrop = DropPaths.ContainsByPredicate([&Entry](const FString& Path)
				{
					return Path.Equals(Entry.Path, ESearchCase::IgnoreCase);
				});
				if (bDrop)
				{
					continue;
				}
				const bool bSubTree = Entry.Match == EEntryMatch::Tree && SubTreeOnly.ContainsByPredicate([&Entry](const FString& Path)
				{
					return Path.Equals(Entry.Path, ESearchCase::IgnoreCase);
				});
				AddEntry(bSubTree ? FEntry{ Entry.Path, EEntryMatch::SubTree } : Entry);
			}
		}
	}

	void FMatcher::SetFailClosed(const FString& Reason)
	{
		Trees.Reset();
		SubTrees.Reset();
		Exceptions.Reset();
		PrefixParents.Reset();
		PrefixNames.Reset();
		AncestorDirs.Reset();
		EntryCount = 0;
		bFailClosed = true;
		FailReason = Reason;
	}

	void FMatcher::AddTree(const FString& PackagePath)
	{
		AddEntry(FEntry{ PackagePath, EEntryMatch::Tree });
	}

	void FMatcher::Finalize()
	{
		auto SortKeys = [](TArray<FString>& Array)
		{
			Array.Sort([](const FString& A, const FString& B)
			{
				return CompareKeys(FStringView(A), FStringView(B)) < 0;
			});
		};
		SortKeys(Trees);
		SortKeys(SubTrees);
		SortKeys(Exceptions);
		SortKeys(AncestorDirs);
		// Префиксные записи сортируются по папке-родителю вместе с именами (перестановка индексов).
		TArray<int32> Order;
		Order.Reserve(PrefixParents.Num());
		for (int32 Index = 0; Index < PrefixParents.Num(); ++Index)
		{
			Order.Add(Index);
		}
		Order.Sort([this](int32 A, int32 B)
		{
			const int32 Cmp = CompareKeys(FStringView(PrefixParents[A]), FStringView(PrefixParents[B]));
			return Cmp != 0 ? Cmp < 0 : CompareKeys(FStringView(PrefixNames[A]), FStringView(PrefixNames[B])) < 0;
		});
		TArray<FString> SortedParents;
		TArray<FString> SortedNames;
		SortedParents.Reserve(Order.Num());
		SortedNames.Reserve(Order.Num());
		for (const int32 Index : Order)
		{
			SortedParents.Add(PrefixParents[Index]);
			SortedNames.Add(PrefixNames[Index]);
		}
		PrefixParents = MoveTemp(SortedParents);
		PrefixNames = MoveTemp(SortedNames);
	}

	EOwnership FMatcher::ClassifyKey(FStringView Key) const
	{
		if (Key.Len() == 0)
		{
			return EOwnership::None;
		}
		if (bFailClosed)
		{
			// Список не прочитан: всё под /Game — логин (как Python).
			return (Key.Equals(FStringView(TEXT("/game")), ESearchCase::CaseSensitive)
				|| Key.StartsWith(FStringView(TEXT("/game/")), ESearchCase::CaseSensitive)) ? EOwnership::Owned : EOwnership::None;
		}
		if (SortedContains(Exceptions, Key))
		{
			return EOwnership::Exception; // общий с игрой пакет — проверяется первым, как в Python
		}
		// Границы сегментов: каждый префикс пути (и путь целиком) — кандидат в записи tree/subtree; для префиксных записей
		// префикс — папка-родитель, а остаток сравнивается с началом имени.
		for (int32 Index = 1; Index <= Key.Len(); ++Index)
		{
			if (Index != Key.Len() && Key[Index] != TEXT('/'))
			{
				continue;
			}
			const FStringView Head = Key.Left(Index);
			if (SortedContains(Trees, Head))
			{
				return EOwnership::Owned;
			}
			if (Index < Key.Len())
			{
				if (SortedContains(SubTrees, Head))
				{
					return EOwnership::Owned;
				}
				const FStringView Rest = Key.RightChop(Index + 1);
				for (int32 Entry = SortedLowerBound(PrefixParents, Head); Entry < PrefixParents.Num(); ++Entry)
				{
					if (CompareKeys(FStringView(PrefixParents[Entry]), Head) != 0)
					{
						break;
					}
					if (Rest.StartsWith(FStringView(PrefixNames[Entry]), ESearchCase::CaseSensitive))
					{
						return EOwnership::Owned;
					}
				}
			}
		}
		return EOwnership::None;
	}

	EOwnership FMatcher::Classify(FStringView PackagePath) const
	{
		TStringBuilder<256> Key;
		MakeKey(PackagePath, Key);
		return ClassifyKey(Key.ToView());
	}

	bool FMatcher::IsAncestorKey(FStringView Key) const
	{
		if (bFailClosed)
		{
			return false; // всё под /Game и так «логин»; выше /Game список ничего не знает
		}
		return SortedContains(AncestorDirs, Key);
	}

	FString FMatcher::Describe() const
	{
		if (bFailClosed)
		{
			return FString::Printf(TEXT("fail-closed (%s): защищается ВСЁ под /Game"), *FailReason);
		}
		return FString::Printf(TEXT("записей %d (дерево %d, поддерево %d, префикс %d, исключений %d), папок-родителей %d"),
			EntryCount, Trees.Num(), SubTrees.Num(), PrefixParents.Num(), Exceptions.Num(), AncestorDirs.Num());
	}

	// ★ 16.09.2026 (login_writers_scan.md, раздел 9). Список для инструментов редактора: один на процесс, строится лениво.
	//   Магическая статика C++11 инициализируется потокобезопасно; дальше объект только читается.
	const FMatcher& GetSharedMatcher()
	{
		static const FMatcher Shared = []()
		{
			FMatcher Built;
			FParsedList Parsed;
			FString Error;
			if (LoadFile(GetProjectListPath(), Parsed, Error))
			{
				Built.Build(Parsed, /*SubTreeOnly*/ {}, /*DropPaths*/ {});
			}
			else
			{
				// Fail-closed, как в Python: список не прочитан → ВСЁ под /Game считается сценой логина.
				Built.SetFailClosed(Error);
			}
			Built.Finalize();
			return Built;
		}();
		return Shared;
	}

	bool IsAssetWriteAllowedByEnv()
	{
		return FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_ALLOW_LOGIN_WRITE")).TrimStartAndEnd() == TEXT("1");
	}

	bool IsLoginPackageBlocked(FStringView PackagePath)
	{
		// Разрешение проверяется ПЕРВЫМ: с ASDA_ALLOW_LOGIN_WRITE=1 инструменты работают как раньше, список не читается вовсе.
		if (IsAssetWriteAllowedByEnv())
		{
			return false;
		}
		return GetSharedMatcher().Classify(PackagePath) == EOwnership::Owned;
	}
}

#endif // WITH_EDITOR
