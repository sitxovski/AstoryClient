// Copyright (c) 2026 @sitxovski (Discord)
// ★ [ПОРТ] 10.10.2026 (полнота английской версии, `Reports/owner_1010/en_completeness.md`). Назначение и правила —
//   в шапке `Public/Text/AsdaLang.h`, раздел «НАДПИСИ ВИДЖЕТОВ БЕЗ id GameText» и «КАРТИНКИ С НАДПИСЯМИ В PIE».
//   Здесь же — выгрузка всех вбитых в WBP текстов с кириллицей/хангылем (`asda.Lang.DumpUiLiterals`): источник для
//   генератора `Tools/asda_ui_literals_lang.py` (ключ текста + русский исходник), только чтение.
#include "Text/AsdaLang.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/PolyglotTextData.h"
#include "Internationalization/Text.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "RHICommandList.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UObject/GCObject.h"
#include "UObject/UnrealType.h"
#if WITH_EDITOR
#include "Editor.h"   // FEditorDelegates — конец PIE (возврат текстур и предпросмотра языка)
#endif

DEFINE_LOG_CATEGORY_STATIC(LogAsdaLangUi, Log, All);

namespace AsdaLangUiPriv
{
	// ── НАДПИСИ ВИДЖЕТОВ ──
	static bool bLiteralsRegistered = false;   // полиглот-данные уже отданы движку (раз на процесс)
	static bool bPreviewOn = false;            // ApplyUiLiterals включил предпросмотр языка игры (PIE)
	static int32 LiteralsCount = 0;            // сколько текстов зарегистрировано

	// Значение ячейки оверлея → строка: экранирование генератора (`\\n`, `\\r`, `\\t`, `\\\\`) снимается — исходник
	//   обязан совпасть с FText ассета побайтно, иначе движок не признает перевод (сверка хэша исходника).
	static FString Unescape(const FString& S)
	{
		FString Out;
		Out.Reserve(S.Len());
		for (int32 i = 0; i < S.Len(); ++i)
		{
			const TCHAR C = S[i];
			if (C == TEXT('\\') && i + 1 < S.Len())
			{
				const TCHAR N = S[i + 1];
				if (N == TEXT('n'))  { Out.AppendChar(TEXT('\n')); ++i; continue; }
				if (N == TEXT('r'))  { Out.AppendChar(TEXT('\r')); ++i; continue; }
				if (N == TEXT('t'))  { Out.AppendChar(TEXT('\t')); ++i; continue; }
				if (N == TEXT('\\')) { Out.AppendChar(TEXT('\\')); ++i; continue; }
			}
			Out.AppendChar(C);
		}
		return Out;
	}

	// Обратное к `Unescape` (выгрузка текстов в TSV: одна строка файла = один текст).
	static FString Escape(const FString& S)
	{
		FString Out;
		Out.Reserve(S.Len() + 8);
		for (TCHAR C : S)
		{
			if (C == TEXT('\\'))      { Out += TEXT("\\\\"); }
			else if (C == TEXT('\n')) { Out += TEXT("\\n"); }
			else if (C == TEXT('\r')) { Out += TEXT("\\r"); }
			else if (C == TEXT('\t')) { Out += TEXT("\\t"); }
			else                      { Out.AppendChar(C); }
		}
		return Out;
	}

	// Таблица оверлея языка L (без цепочки) или nullptr.
	static UDataTable* LoadOverlay(EAsdaLang L, const TCHAR* Table)
	{
		const FString Path = FAsdaLang::OverlayPath(L, Table);
		if (!FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Path))) { return nullptr; }
		return LoadObject<UDataTable>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	// Ключ строки оверлея → (пространство имён, ключ текста): «ns|key», без «|» — пустое пространство имён.
	static void SplitRowKey(const FString& Row, FString& OutNs, FString& OutKey)
	{
		if (!Row.Split(TEXT("|"), &OutNs, &OutKey)) { OutNs.Empty(); OutKey = Row; }
	}

	// ── КАРТИНКИ В PIE ──
	// Пара «RU-текстура → её L10N-вариант» и состояние копии (поля `LastDst`/`State` пишет поток рендера).
	struct FTexPair
	{
		TObjectPtr<UTexture2D> Ru;            // текстура, которую рисуют виджеты/материалы (/Game/X)
		TObjectPtr<UTexture2D> Loc;           // её языковой вариант (/Game/L10N/<lang>/X)
		FString Path;                         // путь RU-пакета (журнал/самотест)
		TSharedRef<TAtomic<int32>, ESPMode::ThreadSafe> State = MakeShared<TAtomic<int32>, ESPMode::ThreadSafe>(0);   // 0 ждёт, 1 скопировано, 2 отклонено
		TSharedRef<FString, ESPMode::ThreadSafe> Why = MakeShared<FString, ESPMode::ThreadSafe>();   // причина отказа (пишет поток рендера до State=2)
		TSharedRef<TAtomic<FRHITexture*>, ESPMode::ThreadSafe> LastDst = MakeShared<TAtomic<FRHITexture*>, ESPMode::ThreadSafe>(nullptr);   // ресурс RU, в который уже скопировано
	};

	// Держатель пар на время PIE (сборщик мусора не выгружает текстуры, пока идёт подмена).
	class FTexHolder : public FGCObject
	{
	public:
		TArray<FTexPair> Pairs;               // найденные пары
		EAsdaLang Lang = EAsdaLang::Ru;       // язык, под который сделана подмена
		FTSTicker::FDelegateHandle Tick;      // повтор копии после пересоздания ресурса (стриминг)

		virtual void AddReferencedObjects(FReferenceCollector& Collector) override
		{
			for (FTexPair& P : Pairs) { Collector.AddReferencedObject(P.Ru); Collector.AddReferencedObject(P.Loc); }
		}
		virtual FString GetReferencerName() const override { return TEXT("AsdaLangPieTextures"); }
	};
	static FTexHolder* GTex = nullptr;   // живёт между ApplyPieTextures и RestorePieTextures

	// Заказать копию пикселей L10N-варианта в GPU-ресурс RU-текстуры (поток рендера). Ресурс, в который копия уже
	//   сделана, повторно не трогается; несовпадение размера/формата/мипов — отказ с причиной (стриминг ещё не догрузил
	//   мипы — повтор на следующем такте, пока не совпадёт или не истечёт ожидание).
	static void EnqueueCopy(const FTexPair& P, bool bFinalTry)
	{
		// ★ Текстура ещё собирается (асинхронная сборка редактора): её ресурс — ОБЩАЯ заглушка движка, одна на многие
		//   текстуры (пара указывала бы на один и тот же RHI — копия «в себя» роняет редактор). Ждать конца сборки.
		if (!P.Ru || !P.Loc) { return; }
#if WITH_EDITOR   // ★ античит А2 (12.10.2026, сборка Shipping): `IsDefaultTexture` есть только в редакторе — в упакованной игре текстура уже собрана
		if (P.Ru->IsDefaultTexture() || P.Loc->IsDefaultTexture()) { return; }
#endif
		FTextureResource* RuRes = P.Ru->GetResource();
		FTextureResource* LocRes = P.Loc->GetResource();
		if (!RuRes || !LocRes) { return; }
		ENQUEUE_RENDER_COMMAND(AsdaLangPieTexCopy)(
			[RuRes, LocRes, State = P.State, Why = P.Why, LastDst = P.LastDst, bFinalTry](FRHICommandListImmediate& RHICmdList)
			{
				FRHITexture* Dst = RuRes->GetTexture2DRHI();
				FRHITexture* Src = LocRes->GetTexture2DRHI();
				if (!Dst || !Src || Dst == Src) { return; }   // один RHI на двоих — заглушка движка, копировать нечего
				if (LastDst->Load() == Dst) { return; }   // этот ресурс уже английский
				const FRHITextureDesc& D = Dst->GetDesc();
				const FRHITextureDesc& S = Src->GetDesc();
				// ★ 10.10.2026 (en_terms_fix, МЕЛК-2): разный флаг sRGB у пары — копия дала бы в PIE картинку темнее/светлее
				//   (вне редактора такого нет: там движок грузит сам L10N-пакет). Это свойство ассета, а не стриминга, —
				//   отказ сразу окончательный, с предупреждением в журнал (один раз: пару в состоянии 2 тикер не трогает).
				const bool bDstSrgb = EnumHasAnyFlags(D.Flags, TexCreate_SRGB);   // RU-ресурс в sRGB
				const bool bSrcSrgb = EnumHasAnyFlags(S.Flags, TexCreate_SRGB);   // L10N-ресурс в sRGB
				if (bDstSrgb != bSrcSrgb)
				{
					*Why = FString::Printf(TEXT("sRGB: RU %s, L10N %s"), bDstSrgb ? TEXT("да") : TEXT("нет"), bSrcSrgb ? TEXT("да") : TEXT("нет"));
					*State = 2;
					UE_LOG(LogAsdaLangUi, Warning, TEXT("[язык] PIE: пара картинок не скопирована — %s (исправить флаг sRGB у L10N-варианта)"), **Why);
					return;
				}
				if (D.Extent != S.Extent || D.Format != S.Format || D.NumMips != S.NumMips || D.ArraySize != S.ArraySize
					|| EnumHasAnyFlags(D.Flags, TexCreate_Virtual) || EnumHasAnyFlags(S.Flags, TexCreate_Virtual))
				{
					if (bFinalTry)
					{
						*Why = FString::Printf(TEXT("RU %dx%d формат %d мипов %d, L10N %dx%d формат %d мипов %d%s"),
							D.Extent.X, D.Extent.Y, (int32)D.Format, D.NumMips, S.Extent.X, S.Extent.Y, (int32)S.Format, S.NumMips,
							(EnumHasAnyFlags(D.Flags, TexCreate_Virtual) || EnumHasAnyFlags(S.Flags, TexCreate_Virtual)) ? TEXT(", виртуальная") : TEXT(""));
						*State = 2;
					}
					return;
				}
				RHICmdList.Transition({ FRHITransitionInfo(Src, ERHIAccess::Unknown, ERHIAccess::CopySrc),
				                        FRHITransitionInfo(Dst, ERHIAccess::Unknown, ERHIAccess::CopyDest) });
				FRHICopyTextureInfo Info;
				Info.NumMips = D.NumMips;
				RHICmdList.CopyTexture(Src, Dst, Info);
				RHICmdList.Transition({ FRHITransitionInfo(Src, ERHIAccess::CopySrc, ERHIAccess::SRVMask),
				                        FRHITransitionInfo(Dst, ERHIAccess::CopyDest, ERHIAccess::SRVMask) });
				LastDst->Store(Dst);
				*State = 1;
			});
	}

#if WITH_EDITOR
	// Конец PIE: картинки и тексты — назад к ассетам редактора (подписка один раз на процесс).
	static void OnEndPie(bool)
	{
		FAsdaLang::RestorePieTextures();
		FAsdaLang::EndPieUiLiterals();
	}
	static void EnsureEndPieHook()
	{
		static bool bHooked = false;
		if (!bHooked) { bHooked = true; FEditorDelegates::EndPIE.AddStatic(&OnEndPie); }
	}
#endif
}

int32 FAsdaLang::ApplyUiLiterals()
{
	using namespace AsdaLangUiPriv;
	if (IsRunningCommandlet()) { return 0; }
	const bool bEditor = GIsEditor;
	// Вне PIE в редакторе — никогда (переводы показались бы в окнах редактора); русская сессия вне PIE — незачем.
	if (bEditor && !IsPieSession()) { return 0; }
	if (!bEditor && IsRu() && !bLiteralsRegistered) { return 0; }

	int32 N = 0;
	if (!bLiteralsRegistered)
	{
		bLiteralsRegistered = true;
		// Переводы всех выбираемых языков (культура UE выберет сама; смена языка на лету — без повторной регистрации).
		//   Нет таблицы языка — его строки берутся из английской (цепочка «язык → en → ru», решение владельца 09.10.2026).
		UDataTable* SrcDt = LoadOverlay(EAsdaLang::En, TEXT("UiLiteralSrc"));
		UDataTable* EnDt = LoadOverlay(EAsdaLang::En, TEXT("UiLiteral"));
		UDataTable* ArDt = LoadOverlay(EAsdaLang::Ar, TEXT("UiLiteral"));
		if (!SrcDt || !EnDt)
		{
			UE_LOG(LogAsdaLangUi, Log, TEXT("[язык] надписи виджетов: оверлеев UiLiteral/UiLiteralSrc нет — тексты WBP как в ассетах"));
			return 0;
		}
		TArray<FPolyglotTextData> Data;
		Data.Reserve(SrcDt->GetRowMap().Num());
		for (const TPair<FName, uint8*>& It : SrcDt->GetRowMap())
		{
			const FAsdaLocRow* Src = reinterpret_cast<const FAsdaLocRow*>(It.Value);
			const FAsdaLocRow* En = EnDt->FindRow<FAsdaLocRow>(It.Key, TEXT("UiLiteral"), false);
			if (!Src || Src->Text.IsEmpty() || !En || En->Text.IsEmpty()) { continue; }
			FString Ns, Key;
			SplitRowKey(It.Key.ToString(), Ns, Key);
			FPolyglotTextData D(ELocalizedTextSourceCategory::Game, Ns, Key, Unescape(Src->Text), TEXT("ru"));
			D.AddLocalizedString(TEXT("en"), Unescape(En->Text));
			const FAsdaLocRow* Ar = ArDt ? ArDt->FindRow<FAsdaLocRow>(It.Key, TEXT("UiLiteral"), false) : nullptr;
			D.AddLocalizedString(TEXT("ar"), Unescape(Ar && !Ar->Text.IsEmpty() ? Ar->Text : En->Text));
			Data.Add(MoveTemp(D));
		}
		FTextLocalizationManager::Get().RegisterPolyglotTextData(Data, /*bAddDisplayStrings*/ true);
		LiteralsCount = N = Data.Num();
		UE_LOG(LogAsdaLangUi, Log, TEXT("[язык] надписи виджетов: зарегистрировано %d текстов (полиглот, родная культура ru)"), N);
	}
#if WITH_EDITOR
	// PIE: игровые тексты в редакторе показываются на языке ПРЕДПРОСМОТРА — включаем его на время сеанса.
	if (bEditor)
	{
		EnsureEndPieHook();
		if (!IsRu())
		{
			FTextLocalizationManager::Get().EnableGameLocalizationPreview(Code(Current()));
			bPreviewOn = true;
			UE_LOG(LogAsdaLangUi, Log, TEXT("[язык] PIE: предпросмотр языка игры «%s» включён (надписи виджетов; выключится в конце PIE)"), Code(Current()));
		}
		else if (bPreviewOn)
		{
			FTextLocalizationManager::Get().DisableGameLocalizationPreview();
			bPreviewOn = false;
		}
	}
#endif
	return N;
}

void FAsdaLang::EndPieUiLiterals()
{
#if WITH_EDITOR
	using namespace AsdaLangUiPriv;
	if (!bPreviewOn) { return; }
	bPreviewOn = false;
	FTextLocalizationManager::Get().DisableGameLocalizationPreview();
	UE_LOG(LogAsdaLangUi, Log, TEXT("[язык] PIE окончен: предпросмотр языка игры выключен — надписи виджетов снова из ассетов"));
#endif
}

int32 FAsdaLang::ApplyPieTextures()
{
	using namespace AsdaLangUiPriv;
#if WITH_EDITOR
	if (!GIsEditor || !IsPieSession() || IsRunningCommandlet()) { return 0; }
	EnsureEndPieHook();
	const EAsdaLang L = Current();
	if (GTex && GTex->Lang == L) { return 0; }   // уже подменено под этот язык
	RestorePieTextures();
	if (L == EAsdaLang::Ru) { return 0; }

	GTex = new FTexHolder();
	GTex->Lang = L;
	const FString Root = FString::Printf(TEXT("/Game/L10N/%s"), Code(L));
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FAssetData> A;
	AR.GetAssetsByPath(FName(*Root), A, /*bRecursive*/ true);
	for (const FAssetData& D : A)
	{
		if (D.AssetClassPath != UTexture2D::StaticClass()->GetClassPathName()) { continue; }
		const FString LocPkg = D.PackageName.ToString();                       // /Game/L10N/en/AsdaAssets/…/n_03
		const FString RuPkg = TEXT("/Game") + LocPkg.Mid(Root.Len());           // /Game/AsdaAssets/…/n_03
		const FString Obj = D.AssetName.ToString();
		if (!FPackageName::DoesPackageExist(RuPkg)) { continue; }
		UTexture2D* Ru = LoadObject<UTexture2D>(nullptr, *(RuPkg + TEXT(".") + Obj), nullptr, LOAD_NoWarn | LOAD_Quiet);
		UTexture2D* Loc = Cast<UTexture2D>(D.GetAsset());
		if (!Ru || !Loc) { continue; }
		// Все мипы в памяти на время PIE (иначе стриминг дозагрузил бы RU-мипы поверх копии).
		Ru->SetForceMipLevelsToBeResident(1.0e7f);
		Loc->SetForceMipLevelsToBeResident(1.0e7f);
		FTexPair P;
		P.Ru = Ru; P.Loc = Loc; P.Path = RuPkg;
		GTex->Pairs.Add(MoveTemp(P));
	}
	for (const FTexPair& P : GTex->Pairs) { EnqueueCopy(P, false); }
	// Повтор: ресурс пересоздан стримингом или сборка текстуры закончилась — скопировать снова; через 60 с несовпадение
	//   формата у СОБРАННЫХ текстур = окончательный отказ (пока текстура собирается, `EnqueueCopy` её пропускает).
	const double T0 = FPlatformTime::Seconds();
	GTex->Tick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([T0](float) -> bool
	{
		if (!GTex) { return false; }
		const bool bFinal = FPlatformTime::Seconds() - T0 > 60.0;
		for (const FTexPair& P : GTex->Pairs) { if (P.State->Load() != 2) { EnqueueCopy(P, bFinal); } }
		return true;
	}), 0.5f);
	UE_LOG(LogAsdaLangUi, Log, TEXT("[язык] PIE: картинки с надписями «%s» — пар %d (копия в видеопамять RU-текстур; ассеты не меняются)"),
		Code(L), GTex->Pairs.Num());
	return GTex->Pairs.Num();
#else
	return 0;
#endif
}

void FAsdaLang::RestorePieTextures()
{
	using namespace AsdaLangUiPriv;
	if (!GTex) { return; }
	FTSTicker::GetCoreTicker().RemoveTicker(GTex->Tick);
	// ★ 10.10.2026 (en_terms_fix, МЕЛК-1): сначала дождаться копий, уже заказанных потоку рендера. Иначе первая копия
	//   пары, стоящая в очереди, выполнилась бы ПОСЛЕ чтения `State`/`LastDst` ниже: пара сочлась бы «не тронутой»,
	//   `UpdateResource` не вызвался бы, и RU-текстура в редакторе осталась бы английской. Новых копий уже не будет —
	//   тикер снят строкой выше.
	FlushRenderingCommands();
	int32 N = 0;
	for (FTexPair& P : GTex->Pairs)
	{
		if (P.Ru)
		{
			P.Ru->SetForceMipLevelsToBeResident(0.f);
			if (P.State->Load() == 1 || P.LastDst->Load() != nullptr) { P.Ru->UpdateResource(); ++N; }   // ресурс заново из данных ассета
		}
		if (P.Loc) { P.Loc->SetForceMipLevelsToBeResident(0.f); }
	}
	FlushRenderingCommands();
	UE_LOG(LogAsdaLangUi, Log, TEXT("[язык] картинки с надписями: RU-ресурсы восстановлены — %d из %d пар"), N, GTex->Pairs.Num());
	delete GTex;
	GTex = nullptr;
}

void FAsdaLang::PieTextureStats(int32& OutPairs, int32& OutCopied, int32& OutRejected)
{
	using namespace AsdaLangUiPriv;
	OutPairs = OutCopied = OutRejected = 0;
	if (!GTex) { return; }
	OutPairs = GTex->Pairs.Num();
	for (const FTexPair& P : GTex->Pairs)
	{
		const int32 S = P.State->Load();
		if (S == 1) { ++OutCopied; }
		if (S == 2)
		{
			++OutRejected;
			UE_LOG(LogAsdaLangUi, Warning, TEXT("[язык] PIE: картинка %s не подменена: %s"), *P.Path, **P.Why);
		}
	}
}

void FAsdaLang::PieTexturePairs(TArray<TWeakObjectPtr<UTexture2D>>& OutRu, TArray<TWeakObjectPtr<UTexture2D>>& OutLoc, TArray<bool>& OutCopied)
{
	using namespace AsdaLangUiPriv;
	OutRu.Reset();
	OutLoc.Reset();
	OutCopied.Reset();
	if (!GTex) { return; }
	for (const FTexPair& P : GTex->Pairs)
	{
		OutRu.Add(P.Ru.Get());
		OutLoc.Add(P.Loc.Get());
		// ★ 10.10.2026 (terms_minors): «копия была» — ровно условие `UpdateResource` в `RestorePieTextures`.
		OutCopied.Add(P.State->Load() == 1 || P.LastDst->Load() != nullptr);
	}
}

// ── Выгрузка вбитых текстов WBP (только чтение) ──
namespace AsdaLangUiDump
{
	// Кириллица или хангыль (корейские заглушки дизайнера) — текст, который игрок не должен видеть в нерусской сессии.
	static bool NeedsTranslation(const FString& S)
	{
		for (TCHAR C : S)
		{
			if ((C >= 0x0400 && C <= 0x04FF) || (C >= 0xAC00 && C <= 0xD7A3) || (C >= 0x1100 && C <= 0x11FF) || (C >= 0x3130 && C <= 0x318F)) { return true; }
		}
		return false;
	}

	// Одна строка выгрузки: окно, узел, путь свойства, вид текста, пространство имён, ключ, исходник.
	static void Emit(TArray<FString>& Out, const FString& Asset, const FString& Node, const FString& Prop, const FText& T)
	{
		const FString* Src = FTextInspector::GetSourceString(T);
		const FString Shown = T.ToString();
		const FString S = Src ? *Src : Shown;
		if (!NeedsTranslation(S) && !NeedsTranslation(Shown)) { return; }
		const TOptional<FString> Ns = FTextInspector::GetNamespace(T);
		const TOptional<FString> Key = FTextInspector::GetKey(T);
		const TCHAR* Kind = T.IsFromStringTable() ? TEXT("stringtable")
			: T.IsCultureInvariant() ? TEXT("invariant")
			: (!Key.IsSet() || Key->IsEmpty()) ? TEXT("nokey") : TEXT("literal");
		Out.Add(FString::Printf(TEXT("%s\t%s\t%s\t%s\t%s\t%s\t%s"), *Asset, *Node, *Prop, Kind,
			*AsdaLangUiPriv::Escape(Ns.Get(FString())), *AsdaLangUiPriv::Escape(Key.Get(FString())), *AsdaLangUiPriv::Escape(S)));
	}

	// Обход свойств объекта/структуры: FText — в выгрузку; структуры и массивы — вглубь (глубина ≤ 4).
	static void Walk(TArray<FString>& Out, const FString& Asset, const FString& Node, const UStruct* Type, const void* Data, const FString& Prefix, int32 Depth)
	{
		if (Depth > 4) { return; }
		for (TFieldIterator<FProperty> It(Type); It; ++It)
		{
			const FProperty* P = *It;
			for (int32 i = 0; i < P->ArrayDim; ++i)
			{
				const void* V = P->ContainerPtrToValuePtr<void>(Data, i);
				const FString Name = Prefix + P->GetName() + (P->ArrayDim > 1 ? FString::Printf(TEXT("[%d]"), i) : FString());
				if (const FTextProperty* TP = CastField<FTextProperty>(P))
				{
					Emit(Out, Asset, Node, Name, TP->GetPropertyValue(V));
				}
				else if (const FStructProperty* SP = CastField<FStructProperty>(P))
				{
					Walk(Out, Asset, Node, SP->Struct, V, Name + TEXT("."), Depth + 1);
				}
				else if (const FArrayProperty* AP = CastField<FArrayProperty>(P))
				{
					FScriptArrayHelper H(AP, V);
					for (int32 k = 0; k < H.Num(); ++k)
					{
						const FString EName = FString::Printf(TEXT("%s[%d]"), *Name, k);
						if (const FTextProperty* ETP = CastField<FTextProperty>(AP->Inner)) { Emit(Out, Asset, Node, EName, ETP->GetPropertyValue(H.GetRawPtr(k))); }
						else if (const FStructProperty* ESP = CastField<FStructProperty>(AP->Inner)) { Walk(Out, Asset, Node, ESP->Struct, H.GetRawPtr(k), EName + TEXT("."), Depth + 1); }
					}
				}
			}
		}
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommand GCmdLangDumpUiLiterals(
	TEXT("asda.Lang.DumpUiLiterals"),
	TEXT("[ПОРТ] Выгрузить все тексты WBP (/Game/AsdaAssets/UI/**) с кириллицей/хангылем: окно, узел, свойство, вид (literal/"
	     "stringtable/invariant/nokey), пространство имён, ключ, исходник. [out=<файл.tsv>] [quit]. Только чтение."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		FString OutPath = FPaths::ProjectSavedDir() / TEXT("asda_ui_literals.tsv");
		bool bQuit = false;
		for (const FString& A : Args)
		{
			if (A.StartsWith(TEXT("out="))) { OutPath = A.Mid(4); }
			if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
		}
		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FAssetData> A;
		AR.GetAssetsByPath(FName(TEXT("/Game/AsdaAssets/UI")), A, /*bRecursive*/ true);
		TArray<FString> Lines;
		Lines.Add(TEXT("asset\tnode\tprop\tkind\tns\tkey\tsource"));
		int32 Windows = 0;
		for (const FAssetData& D : A)
		{
			const FString N = D.AssetName.ToString();
			if (!N.StartsWith(TEXT("WBP_"))) { continue; }
			const FString ClsPath = D.GetObjectPathString() + TEXT("_C");
			UWidgetBlueprintGeneratedClass* Cls = LoadObject<UWidgetBlueprintGeneratedClass>(nullptr, *ClsPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Cls) { continue; }
			++Windows;
			const FString Asset = D.PackageName.ToString();
			if (UWidgetTree* T = Cls->GetWidgetTreeArchetype())
			{
				T->ForEachWidget([&](UWidget* W)
				{
					if (W) { AsdaLangUiDump::Walk(Lines, Asset, W->GetName(), W->GetClass(), W, FString(), 0); }
				});
			}
			if (UObject* Cdo = Cls->GetDefaultObject(false))
			{
				AsdaLangUiDump::Walk(Lines, Asset, TEXT("(класс)"), Cls, Cdo, FString(), 0);
			}
		}
		FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")) + TEXT("\n"), *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogAsdaLangUi, Warning, TEXT("ВЫГРУЗКА-НАДПИСЕЙ ИТОГ: окон %d, текстов %d → %s"), Windows, Lines.Num() - 1, *OutPath);
		if (bQuit) { FPlatformMisc::RequestExit(false, TEXT("asda.Lang.DumpUiLiterals")); }
	}));
#endif // ASDA_DEV_CONSOLE
