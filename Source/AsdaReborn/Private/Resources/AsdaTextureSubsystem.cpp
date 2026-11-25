// Copyright (c) 2026 @sitxovski (Discord)
#include "Resources/AsdaTextureSubsystem.h"
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "AsdaTexLoader.h"            // низкоуровневый парсер .TEX (аналог gfxLoadTextureBtz)
#include "Engine/Texture2D.h"
#include "Engine/StreamableRenderAsset.h"   // ★ charsel_preload: реестр владения флагом полных мипов
#include "Misc/Paths.h"
#include "Misc/PackageName.h"   // ★ 10.10.2026: FPackageName::DoesPackageExist — поиск языкового варианта /Game/L10N/<культура>/…

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTexMgr, Log, All);

void UAsdaTextureSubsystem::Deinitialize()
{
	ClearCache();
	Super::Deinitialize();
}

void UAsdaTextureSubsystem::ClearCache()
{
	Cache.Empty();   // низкоуровневого path-кэша парсера больше нет (чтение .TEX с диска убрано)
}

bool UAsdaTextureSubsystem::HasLocalizedVariant(const FString& PackageName)
{
	static const TCHAR* const kCultures[] = { TEXT("en"), TEXT("ar"), TEXT("ru") };   // культуры порта (FAsdaLang::Code)
	if (!PackageName.StartsWith(TEXT("/Game/")) || PackageName.StartsWith(TEXT("/Game/L10N/"))) { return false; }
	const FString Rest = PackageName.Mid(6);
	for (const TCHAR* C : kCultures)
	{
		if (FPackageName::DoesPackageExist(FString::Printf(TEXT("/Game/L10N/%s/%s"), C, *Rest))) { return true; }
	}
	return false;
}

int32 UAsdaTextureSubsystem::ReleaseLocalizedVariants()
{
	int32 N = 0;
	for (auto It = Cache.CreateIterator(); It; ++It)
	{
		const UTexture2D* Tex = It.Value().Tex;
		if (Tex && HasLocalizedVariant(Tex->GetOutermost()->GetName()))
		{
			It.RemoveCurrent();
			++N;
		}
	}
	UE_LOG(LogAsdaTexMgr, Log, TEXT("[текстуры] язык на лету: отпущено из кэша атласов с языковым вариантом — %d"), N);
	return N;
}

// ── Реестр владения флагом полных мипов (см. объявление в .h) ──
namespace AsdaFullMips
{
	struct FEntry
	{
		int32 Count = 0;          // сколько владельцев держат флаг
		bool bExternal = false;   // флаг стоял ДО первого владельца (чужой постоянный форс) — не снимаем
	};
	static TMap<TObjectKey<UStreamableRenderAsset>, FEntry>& Registry()
	{
		static TMap<TObjectKey<UStreamableRenderAsset>, FEntry> R;
		return R;
	}
	static void ReleaseKey(const TObjectKey<UStreamableRenderAsset>& K)
	{
		FEntry* E = Registry().Find(K);
		if (!E || --E->Count > 0) { return; }
		if (!E->bExternal)
		{
			if (UStreamableRenderAsset* A = K.ResolveObjectPtr()) { A->bForceMiplevelsToBeResident = false; }
		}
		Registry().Remove(K);
	}
}

void UAsdaTextureSubsystem::AcquireFullMips(UStreamableRenderAsset* Asset, FFullMipsOwnerSet& Owner)
{
	if (!Asset) { return; }
	const TObjectKey<UStreamableRenderAsset> K(Asset);
	bool bAlready = false;
	Owner.Add(K, &bAlready);
	if (bAlready) { return; }
	AsdaFullMips::FEntry& E = AsdaFullMips::Registry().FindOrAdd(K);
	if (E.Count == 0)
	{
		E.bExternal = Asset->bForceMiplevelsToBeResident;
		Asset->bForceMiplevelsToBeResident = true;
	}
	++E.Count;
}

void UAsdaTextureSubsystem::SyncFullMips(FFullMipsOwnerSet& Owner, const TSet<UStreamableRenderAsset*>& Want)
{
	for (auto It = Owner.CreateIterator(); It; ++It)
	{
		UStreamableRenderAsset* A = It->ResolveObjectPtr();
		if (!A || !Want.Contains(A)) { AsdaFullMips::ReleaseKey(*It); It.RemoveCurrent(); }
	}
	for (UStreamableRenderAsset* A : Want) { AcquireFullMips(A, Owner); }
}

void UAsdaTextureSubsystem::ReleaseAllFullMips(FFullMipsOwnerSet& Owner)
{
	for (const TObjectKey<UStreamableRenderAsset>& K : Owner) { AsdaFullMips::ReleaseKey(K); }
	Owner.Reset();
}

// Записать/обновить слот кэша: первый раз refCount=1, повтор — вернуть существующий с ++refCount.
UTexture2D* UAsdaTextureSubsystem::StoreCached(const FString& CacheKey, UTexture2D* Tex)
{
	// НЕ кэшируем промах (эталон: слот промаха переиспользуется → ретрай при появлении ассета).
	if (!Tex)
	{
		return nullptr;
	}
	FAsdaTexCacheEntry& E = Cache.FindOrAdd(CacheKey);
	E.Tex = Tex;
	++E.RefCount;   // аналог slot+4 (++refCount)
	return Tex;
}

// ★ Довести ассет-текстуру до «готова к использованию ПРЯМО СЕЙЧАС».
//   Две грабли UE, обе дают ОДИН симптом — модель/виджет рисуется «без текстуры» (ровная серо-белая):
//   1) UTexture::IsCompiling() в Engine/Classes/Engine/Texture.h буквально `return IsDefaultTexture();`
//      — пока FTextureCompilingManager собирает текстуру асинхронно (промах DDC), движок отдаёт
//      ДЕФОЛТНУЮ серую 32×32. Ассет, назначенный в материал в этот момент, так серым и рисуется.
//      Проверено зондом: в свежем процессе itm1_n_98_othr102_00 («Сияющая руда») отдаёт 32×32
//      вместо 128×64, и дроп-модель выходит белой болванкой при полностью корректных ассетах.
//   2) Текстуры, которые рантайм ставит в MID (дроп предметов, карта, UI), НЕ входят в стриминг-данные
//      меша ⇒ стример не обязан подтягивать верхние мипы. Держим мипы резидентными принудительно.
void UAsdaTextureSubsystem::MakeReadyForRuntimeUse(UTexture2D* Tex)
{
	if (!Tex)
	{
		return;
	}
#if WITH_EDITOR
	if (Tex->IsCompiling())
	{
		Tex->BlockOnAnyAsyncBuild();   // дождаться реальных мип-данных вместо дефолтной серой
	}
#endif
	Tex->SetForceMipLevelsToBeResident(kForceMipResidentSeconds);
}

UTexture2D* UAsdaTextureSubsystem::ResolveAndCache(const FString& Key)
{
	if (Key.IsEmpty())
	{
		return nullptr;
	}
	// Дедуп РЕГИСТРОНЕЗАВИСИМО (эталон CTextureManager: strlwr+strcmp по имени слота). Ключ кэша — lowercase.
	const FString CacheKey = Key.ToLower();
	if (FAsdaTexCacheEntry* Found = Cache.Find(CacheKey))
	{
		++Found->RefCount;      // дедуп + ++refCount (аналог L5045/L5066 CTextureManager)
		return Found->Tex;
	}

	UTexture2D* Tex = nullptr;
	if (Key.StartsWith(TEXT("/")))
	{
		// Объектный путь UE. Если нет '.LeafName' — дописать (пакет "/A/B/N" → "/A/B/N.N"). ★ волна 2 п. 7: одна точка правила.
		const FString ObjPath = AsdaAssetPaths::ObjectPathFromPackage(Key);
		Tex = LoadObject<UTexture2D>(nullptr, *ObjPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Tex)
		{
			// ★ Имена в таблицах данных приходят в РАЗНОМ регистре («Premium_icon» в special_itemlist
			//   против ассета `premium_icon`), а импортёр сложил всё как есть. Повторяем в нижнем
			//   регистре — иначе иконки записей второй таблицы просто не находятся.
			const FString Lower = ObjPath.ToLower();
			if (Lower != ObjPath)
			{
				Tex = LoadObject<UTexture2D>(nullptr, *Lower, nullptr, LOAD_NoWarn | LOAD_Quiet);
			}
		}
		MakeReadyForRuntimeUse(Tex);
	}
	else
	{
		// ★ Дисковый фолбэк (рантайм-декод .TEX из папки оригинальной игры) УБРАН: рантайм читает данные
		//   ТОЛЬКО из ассетов UE. Файлы оригинала в шипнутый пак не попадают ⇒ ветка всё равно была мёртвой,
		//   но маскировала пропущенный импорт (в редакторе текстура находилась, в билде — нет).
		UE_LOG(LogAsdaTexMgr, Warning,
			TEXT("ResolveAndCache: ключ '%s' — не объектный путь UE. Текстура должна быть ИМПОРТИРОВАНА "
			     "в /Game/AsdaAssets/Textures/... (чтение .TEX с диска в рантайме запрещено)."), *Key);
	}

	return StoreCached(CacheKey, Tex);
}

UTexture2D* UAsdaTextureSubsystem::LoadTextureCached(const FString& Key)
{
	return ResolveAndCache(Key);
}

UTexture2D* UAsdaTextureSubsystem::BuildPathAndLoad(const FString& Src)
{
	if (Src.IsEmpty())
	{
		return nullptr;
	}
	// Объектный/дисковый путь — как есть (расширение значимо). Иначе — имя-источник: срезать чужое расширение
	//   (аналог BuildPathAndLoad: замена расширения) и грузить как UI-текстуру.
	if (Src.StartsWith(TEXT("/")) || Src.EndsWith(TEXT(".TEX")) || Src.EndsWith(TEXT(".tex")))
	{
		return ResolveAndCache(Src);
	}
	return LoadUITexture(FName(*FPaths::GetBaseFilename(Src)));
}

UTexture2D* UAsdaTextureSubsystem::LoadUITexture(FName Name)
{
	if (Name.IsNone())
	{
		return nullptr;
	}
	// Нормализация: срезать любое расширение из имени ("FOO.TGA" → "FOO"). JUI-конвенция: пакет = имя ассета.
	const FString N = FPaths::GetBaseFilename(Name.ToString());
	if (N.IsEmpty())
	{
		return nullptr;
	}
	return ResolveAndCache(AsdaAssetPaths::JuiTextureObject(N));   // ★ волна 2 п. 7: правило в реестре путей
}

UTexture2D* UAsdaTextureSubsystem::LoadMapTexture(const FString& MapDir, const FString& TexName)
{
	if (TexName.IsEmpty())
	{
		return nullptr;
	}
	const FString CacheKey = FString::Printf(TEXT("map:%s:%s"), *MapDir, *TexName).ToLower();  // регистронезав.
	if (FAsdaTexCacheEntry* Found = Cache.Find(CacheKey))
	{
		++Found->RefCount;
		return Found->Tex;
	}

	// Имя detail часто несёт расширение источника ("TILE0.TGA") — берём базу, шипнутый файл = "<base>.TEX".
	const FString BaseName = FPaths::GetBaseFilename(TexName);
	const FString MapName  = FPaths::GetCleanFilename(MapDir);
	UTexture2D* Tex = nullptr;

	// 1) Project-first: импортированные ассеты /Game/AsdaAssets/Textures/Maps/<map>/[Texture/]<base>.<base>
	// ★ ИСПРАВЛЕНИЕ D2 (волна 2 п. 7, 14.09.2026): имя папки карты приводится AsdaMapRegistry::ToAssetName (пробел → «_»).
	//   Было «…/Maps/Mountain Flamion» при папке Mountain_Flamion — project-first промахивался, редактор читал сырой .TEX.
	const FString ProjBase = AsdaAssetPaths::MapTexDir(MapName);
	const FString ProjPkgs[] = { ProjBase / TEXT("Texture"), ProjBase };
	for (const FString& Pkg : ProjPkgs)
	{
		const FString ObjPath = (Pkg / BaseName) + TEXT(".") + BaseName;
		if (UTexture2D* T = LoadObject<UTexture2D>(nullptr, *ObjPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			MakeReadyForRuntimeUse(T);   // см. MakeReadyForRuntimeUse: async-компиляция + резидентность мипов
			Tex = T;
			break;
		}
	}

	// 2) ★ Дисковый фолбэк (.TEX из папки карты оригинала) УБРАН — см. ResolveAndCache. Не найдено в
	//    /Game/AsdaAssets/Textures/Maps/<map>/ ⇒ текстура НЕ импортирована, чинить импортом, не чтением файла.
	if (!Tex)
	{
		// ★ Ревизия 15.09: в логе — фактически проверенная папка (ProjBase, после ToAssetName), а не сырое имя карты
		UE_LOG(LogAsdaTexMgr, Warning, TEXT("LoadMapTexture: '%s' (карта '%s') нет среди импортированных "
			"ассетов %s/"), *BaseName, *MapName, *ProjBase);
	}

	return StoreCached(CacheKey, Tex);
}

UTexture2D* UAsdaTextureSubsystem::LoadTexFromMemory(const uint8* Data, int32 Size, const FString& CacheKey)
{
	const FString Key = CacheKey.IsEmpty() ? FString() : CacheKey.ToLower();   // регистронезав.
	if (!Key.IsEmpty())
	{
		if (FAsdaTexCacheEntry* Found = Cache.Find(Key))
		{
			++Found->RefCount;
			return Found->Tex;
		}
	}
	UTexture2D* Tex = AsdaTex::LoadTexFromMemory(Data, Size);
	if (!Key.IsEmpty())
	{
		return StoreCached(Key, Tex);   // StoreCached сам не кэширует nullptr
	}
	return Tex;
}

bool UAsdaTextureSubsystem::AddRefTexture(const FString& Key)
{
	if (FAsdaTexCacheEntry* Found = Cache.Find(Key.ToLower()))
	{
		++Found->RefCount;
		return true;
	}
	return false;
}

void UAsdaTextureSubsystem::ReleaseTexture(const FString& Key)
{
	const FString CacheKey = Key.ToLower();
	if (FAsdaTexCacheEntry* Found = Cache.Find(CacheKey))
	{
		if (--Found->RefCount <= 0)
		{
			Cache.Remove(CacheKey);   // 0 ссылок — убрать слот (GC освободит текстуру, если больше нет ссылок)
		}
	}
}

int64 UAsdaTextureSubsystem::GetTotalCacheBytes() const
{
	int64 Total = 0;
	for (const TPair<FString, FAsdaTexCacheEntry>& Kv : Cache)
	{
		if (Kv.Value.Tex)
		{
			Total += (int64)Kv.Value.Tex->GetResourceSizeBytes(EResourceSizeMode::EstimatedTotal);
		}
	}
	return Total;
}
