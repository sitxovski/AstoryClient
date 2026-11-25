// Copyright (c) 2026 @sitxovski (Discord)
// Реализация FAsdaLang — назначение и правила в шапке `Public/Text/AsdaLang.h`.
#include "Text/AsdaLang.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "Resources/AsdaGameUserSettings.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "Engine/Engine.h"
#include "Internationalization/StringTable.h"   // UStringTable — подписи окон (этап 4)
#include "Internationalization/StringTableCore.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"            // UPackage::IsDirty — PIE не правит таблицу подписей, изменённую до PIE
#include "Login/AsdaLoginData.h"
#include "Text/AsdaLangRows.h"            // AsdaLangRows::DropsRuPlaceholder — RU-заглушка «Нет» без перевода (gold-boast)
#include "UI/AsdaItemTipStandalone.h"   // ★ [ПОРТ] 29.10.2026: FAsdaLangScope — подмена языка на время сборки карточки предмета
#if WITH_EDITOR
#include "Editor.h"                      // GEditor, FEditorDelegates — перечитывание языка на старте PIE и перезапуск сеанса PIE
#include "PlayInEditorDataTypes.h"       // FRequestPlaySessionParams / FPlayInEditorSessionInfo — параметры сеанса PIE
#endif

DEFINE_LOG_CATEGORY_STATIC(LogAsdaLang, Log, All);

namespace AsdaLangPriv
{
	// Язык сессии и признаки, с которыми он определён (заполняет `InitAtStartup`).
	static bool bInited = false;
	static EAsdaLang Session = EAsdaLang::Ru;
	static bool bSessionOverride = false;   // язык задан командной строкой (-AsdaLang / -culture)
	static bool bOsDefault = false;         // язык не выбран — взят язык системы
	// ★ [ПОРТ] 29.10.2026: подменённый язык данных на время `FAsdaLangScope` (−1 — подмены нет). Только игровой поток.
	static int32 ForcedLang = -1;
	// Секция профиля игрока и ключ поля `UAsdaGameUserSettings::Language` (читаются через GConfig ДО создания объекта
	//   профиля: `StartupModule` идёт раньше `UEngine::Init`).
	static const TCHAR* kSection = TEXT("/Script/AsdaReborn.AsdaGameUserSettings");
	static const TCHAR* kKey = TEXT("Language");
	// Кэш оверлеев: путь → таблица (слабая ссылка, как у `AsdaDt::Cached`) и множество отсутствующих путей
	//   (чтобы не искать пакет на каждом вызове — оверлеи есть не у всех таблиц).
	static TMap<FString, TWeakObjectPtr<UDataTable>> Tables;
	static TSet<FString> Missing;
	// ★ 10.10.2026: сеанс PIE (между `PreBeginPIE` и `EndPIE`) и язык редактора вне PIE (его возвращает `EndPIE`).
	static bool bPie = false;                          // идёт сеанс PIE
	static EAsdaLang EditorSession = EAsdaLang::Ru;    // язык сессии редактора, определённый при старте процесса
	static bool bEditorOsDefault = false;              // у редактора язык не выбран — взят язык системы
	// Исходные строки таблицы подписей до правки в PIE: ключ → (строка, заметка). Пусто — таблица не правилась.
	static TMap<FString, TPair<FString, FString>> PieCaptionOrig;

	// Язык из профиля; профиль пуст → язык системы (русская Windows → русский, иначе английский), bOutOs = true.
	static EAsdaLang FromProfileOrOs(bool& bOutOs)
	{
		EAsdaLang L = EAsdaLang::Ru;
		bOutOs = false;
		if (!FAsdaLang::GetSaved(L))
		{
			EAsdaLang Os = EAsdaLang::En;
			L = (FAsdaLang::ParseCode(FPlatformMisc::GetDefaultLanguage(), Os) && Os == EAsdaLang::Ru) ? EAsdaLang::Ru : EAsdaLang::En;
			bOutOs = true;
		}
		return L;
	}

#if WITH_EDITOR
	// Старт сеанса PIE: язык сессии — из профиля (кроме ключа `-AsdaLang=` редактора). Подсистемы GameInstance
	//   поднимаются ПОСЛЕ этого события, поэтому весь текст сеанса (таблицы, оверлеи, подписи окон) — на новом языке.
	static void OnPreBeginPie(bool /*bSimulating*/)
	{
		bPie = true;
		if (!bSessionOverride)
		{
			bool bOs = false;
			Session = FromProfileOrOs(bOs);
			bOsDefault = bOs;
		}
		UE_LOG(LogAsdaLang, Log, TEXT("[язык] PIE: сессия «%s» (%s); текстуры L10N в редакторе движок не подменяет"),
			FAsdaLang::Code(Session), bSessionOverride ? TEXT("командная строка редактора") : (bOsDefault ? TEXT("не выбран — язык системы") : TEXT("профиль")));
	}

	// Конец сеанса PIE: таблице подписей — русские строки ассета, языку редактора — значение до PIE.
	static void OnEndPie(bool /*bSimulating*/)
	{
		FAsdaLang::RestoreCaptionTableAfterPie();
		bPie = false;
		Session = EditorSession;
		bOsDefault = bEditorOsDefault;
	}
#endif
}

FSimpleMulticastDelegate& FAsdaLang::OnLanguageChanged()
{
	static FSimpleMulticastDelegate D;   // подписчики — подсистемы с кэшами текста (см. объявление)
	return D;
}

bool FAsdaLang::IsPieSession()
{
	return AsdaLangPriv::bPie;
}

const TCHAR* FAsdaLang::Code(EAsdaLang L)
{
	switch (L)
	{
	case EAsdaLang::En: return TEXT("en");
	case EAsdaLang::Ar: return TEXT("ar");
	default:            return TEXT("ru");
	}
}

bool FAsdaLang::ParseCode(const FString& S, EAsdaLang& Out)
{
	const FString C = S.TrimStartAndEnd().Left(2).ToLower();
	if (C == TEXT("ru")) { Out = EAsdaLang::Ru; return true; }
	if (C == TEXT("en")) { Out = EAsdaLang::En; return true; }
	if (C == TEXT("ar")) { Out = EAsdaLang::Ar; return true; }
	return false;
}

int32 FAsdaLang::ToOriginalCode(EAsdaLang L)
{
	switch (L)
	{
	case EAsdaLang::En: return 2;
	case EAsdaLang::Ar: return 8;
	default:            return 7;
	}
}

bool FAsdaLang::GetSaved(EAsdaLang& Out)
{
	// Объект профиля, если уже создан (его поле — источник правды после загрузки), иначе — файл через GConfig.
	FString V;
	if (UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get())
	{
		V = P->Language;
	}
	else if (GConfig)
	{
		GConfig->GetString(AsdaLangPriv::kSection, AsdaLangPriv::kKey, V, GGameUserSettingsIni);
	}
	if (V.IsEmpty())
	{
		return false;
	}
	// ★ 09.10.2026 (ревизия мультиязычности МЕЛК-4): язык, недоступный для выбора (арабский — решение владельца 09.10),
	//   из профиля НЕ принимается: ручная правка ini `Language=ar` иначе давала сессию `Ar` и пункт настроек вне списка.
	//   Такой профиль = «язык не выбран» (окно первого выбора, до него — язык системы). Разбор во временную переменную:
	//   вызывающий (`AsdaSystemOptionSubsystem`) читает `Out` и при false — значение не должно поменяться.
	EAsdaLang L = EAsdaLang::Ru;
	if (!ParseCode(V, L))
	{
		return false;
	}
	if (!IsSelectable(L))
	{
		static bool bWarned = false;   // строка в журнал — один раз за процесс (GetSaved зовут многократно)
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogAsdaLang, Warning, TEXT("[язык] в профиле «%s» — язык недоступен для выбора, считается «не выбран»"), *V);
		}
		return false;
	}
	Out = L;
	return true;
}

void FAsdaLang::SetSaved(EAsdaLang L, bool bSave, const TCHAR* When)
{
	if (UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get())
	{
		P->Language = Code(L);
		if (bSave) { P->SaveSettings(); }
	}
	else if (GConfig)
	{
		GConfig->SetString(AsdaLangPriv::kSection, AsdaLangPriv::kKey, Code(L), GGameUserSettingsIni);
		if (bSave) { GConfig->Flush(false, GGameUserSettingsIni); }
	}
	UE_LOG(LogAsdaLang, Log, TEXT("[язык] в профиль записан «%s» (%s; сессия — «%s»)"), Code(L), When ? When : TEXT("действует после перезапуска"), Code(Current()));
}

void FAsdaLang::InitAtStartup()
{
	using namespace AsdaLangPriv;
	if (bInited) { return; }
	bInited = true;

	// Коммандлеты (запись ассетов) — всегда русский: инструмент не должен «запечь» перевод в RU-ассет.
	if (IsRunningCommandlet())
	{
		Session = EAsdaLang::Ru;
		UE_LOG(LogAsdaLang, Log, TEXT("[язык] коммандлет — сессия на русском, культура не трогается"));
		return;
	}
	FString Arg;
	EAsdaLang L = EAsdaLang::Ru;
	if (FParse::Value(FCommandLine::Get(), TEXT("-AsdaLang="), Arg) && ParseCode(Arg, L))
	{
		bSessionOverride = true;
	}
	else if (!GIsEditor && FParse::Value(FCommandLine::Get(), TEXT("-culture="), Arg) && ParseCode(Arg, L))
	{
		bSessionOverride = true;   // штатный ключ движка: культура уже выставлена движком, язык данных — по ней
	}
	else
	{
		// Профиль; язык ещё не выбран — до выбора в окне первого запуска язык системы (русская Windows → русский,
		//   иначе английский; рекомендация плана Р1). Совпал выбор с ним — применять на лету нечего.
		L = FromProfileOrOs(bOsDefault);
	}
	Session = L;

	// Культура UE — только вне редактора: в редакторе это язык интерфейса самого редактора.
	if (!GIsEditor)
	{
		const bool bOk = FInternationalization::Get().SetCurrentCulture(Code(L));
		UE_LOG(LogAsdaLang, Log, TEXT("[язык] сессия «%s» (%s), культура UE %s → «%s»"), Code(L),
			bSessionOverride ? TEXT("командная строка") : (bOsDefault ? TEXT("не выбран — язык системы") : TEXT("профиль")),
			bOk ? TEXT("выставлена") : TEXT("НЕ выставлена"), *FInternationalization::Get().GetCurrentCulture()->GetName());
	}
	else
	{
		UE_LOG(LogAsdaLang, Log, TEXT("[язык] редактор: язык данных «%s», культура не трогается"), Code(L));
		EditorSession = L;
		bEditorOsDefault = bOsDefault;
#if WITH_EDITOR
		// ★ 10.10.2026: язык PIE — из профиля на КАЖДОМ старте PIE (прежде PIE жил языком старта редактора).
		FEditorDelegates::PreBeginPIE.AddStatic(&AsdaLangPriv::OnPreBeginPie);
		FEditorDelegates::EndPIE.AddStatic(&AsdaLangPriv::OnEndPie);
#endif
	}
}

EAsdaLang FAsdaLang::Current()
{
	if (!AsdaLangPriv::bInited) { InitAtStartup(); }
	// ★ [ПОРТ] 29.10.2026: подмена языка на время сборки карточки просмотрщика предметов (`FAsdaLangScope`,
	//   UI/AsdaItemTipStandalone.h). Вне области подмены (−1) — язык сессии, как прежде.
	if (AsdaLangPriv::ForcedLang >= 0) { return (EAsdaLang)AsdaLangPriv::ForcedLang; }
	return AsdaLangPriv::Session;
}

// ★ [ПОРТ] 29.10.2026: область подмены языка данных (объявление — UI/AsdaItemTipStandalone.h).
FAsdaLangScope::FAsdaLangScope(EAsdaLang L)
{
	check(IsInGameThread());
	Prev = AsdaLangPriv::ForcedLang;
	AsdaLangPriv::ForcedLang = (int32)L;
}

FAsdaLangScope::~FAsdaLangScope()
{
	AsdaLangPriv::ForcedLang = Prev;
}

bool FAsdaLang::IsSessionOverride()
{
	if (!AsdaLangPriv::bInited) { InitAtStartup(); }
	return AsdaLangPriv::bSessionOverride;
}

bool FAsdaLang::IsChosen()
{
	EAsdaLang L;
	return IsSessionOverride() || GetSaved(L);
}

FString FAsdaLang::OverlayPath(EAsdaLang L, const TCHAR* Table)
{
	const TCHAR* C = Code(L);
	return FString::Printf(TEXT("/Game/AsdaAssets/Text/L10N/%s/DT_Loc_%s_%s.DT_Loc_%s_%s"), C, Table, C, Table, C);
}

bool FAsdaLang::FindOverlay(EAsdaLang L, const TCHAR* Table, const FString& Key, FString& Out)
{
	using namespace AsdaLangPriv;
	if (L == EAsdaLang::Ru) { return false; }   // у русского оверлеев нет: русский — это сами ассеты
	const FString Path = OverlayPath(L, Table);
	if (Missing.Contains(Path)) { return false; }
	UDataTable* Dt = nullptr;
	if (TWeakObjectPtr<UDataTable>* P = Tables.Find(Path)) { Dt = P->Get(); }
	if (!Dt)
	{
		const FString Pkg = FPackageName::ObjectPathToPackageName(Path);
		if (!FPackageName::DoesPackageExist(Pkg))
		{
			Missing.Add(Path);
			return false;
		}
		Dt = LoadObject<UDataTable>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Dt)
		{
			Missing.Add(Path);
			UE_LOG(LogAsdaLang, Warning, TEXT("[язык] пакет оверлея есть, но таблица не загрузилась: %s"), *Path);
			return false;
		}
		Tables.Add(Path, Dt);
	}
	const FAsdaLocRow* R = Dt->FindRow<FAsdaLocRow>(FName(*Key), TEXT("FAsdaLang"), /*bWarnIfRowMissing*/false);
	if (!R || R->Text.IsEmpty()) { return false; }
	Out = R->Text;
	return true;
}

FString FAsdaLang::Resolve(const TCHAR* Table, const FString& Key, const FString& RuValue)
{
	const EAsdaLang L = Current();
	if (L == EAsdaLang::Ru) { return RuValue; }
	FString S;
	if (FindOverlay(L, Table, Key, S)) { return S; }
	// Цепочка подстановки (решение владельца 09.10.2026): нет перевода → английский, нет английского → русский.
	if (L != EAsdaLang::En && FindOverlay(EAsdaLang::En, Table, Key, S)) { return S; }
	// ★ [ИСПРАВЛЕНИЕ] 05.11.2026 (gold-boast): русская заглушка «Нет» без перевода — пустая строка (`asda.Lang.DropRuPlaceholder`,
	//   разбор у ключа в AsdaLangRows.cpp).
	if (AsdaLangRows::DropsRuPlaceholder(RuValue)) { return FString(); }
	return RuValue;
}

FString FAsdaLang::ResolveId(const TCHAR* Table, int32 Id, const TCHAR* Field, const FString& RuValue)
{
	if (IsRu()) { return RuValue; }
	const FString Key = (Field && *Field) ? FString::Printf(TEXT("%d.%s"), Id, Field) : FString::FromInt(Id);
	return Resolve(Table, Key, RuValue);
}

FString FAsdaLang::ResolveRow(const UDataTable* Table, const FName& RowName, const TCHAR* Field, const FString& RuValue)
{
	if (IsRu() || !Table) { return RuValue; }
	FString Name = Table->GetName();
	Name.RemoveFromStart(TEXT("DT_"));
	const FString Key = (Field && *Field) ? RowName.ToString() + TEXT(".") + Field : RowName.ToString();
	return Resolve(*Name, Key, RuValue);
}

bool FAsdaLang::RestartGame()
{
	if (IsRunningCommandlet()) { return false; }
	if (GIsEditor)
	{
#if WITH_EDITOR
		// PIE: перезапуск СЕАНСА с теми же параметрами. Остановка — отложенная (`RequestEndPlayMap`, следующим тиком
		//   редактора), новый сеанс заказывается из `ShutdownPIE` — после полной разборки прежнего мира. Настройки игры
		//   в редакторе (`EditorPlaySettings`) — текущие: копия прежнего сеанса к этому моменту уже отпущена.
		if (AsdaLangPriv::bPie && GEditor && GEditor->IsPlayingSessionInEditor())
		{
			const TOptional<FPlayInEditorSessionInfo> Info = GEditor->GetPlayInEditorSessionInfo();
			if (!Info.IsSet()) { return false; }
			FRequestPlaySessionParams Params = Info->OriginalRequestParams;
			Params.EditorPlaySettings = nullptr;
			static FDelegateHandle Pending;   // одна заявка на перезапуск; повторное согласие её не дублирует
			if (Pending.IsValid()) { return true; }
			Pending = FEditorDelegates::ShutdownPIE.AddLambda([Params](bool)
			{
				FEditorDelegates::ShutdownPIE.Remove(Pending);
				Pending.Reset();
				if (GEditor)
				{
					GEditor->RequestPlaySession(Params);
					UE_LOG(LogAsdaLang, Log, TEXT("[язык] PIE: новый сеанс заказан (язык — из профиля)"));
				}
			});
			UE_LOG(LogAsdaLang, Log, TEXT("[язык] PIE: перезапуск сеанса (процесс редактора не перезапускается)"));
			GEditor->RequestEndPlayMap();
			return true;
		}
#endif
		UE_LOG(LogAsdaLang, Warning, TEXT("[язык] перезапуск: редактор вне PIE — нечего перезапускать"));
		return false;
	}
	// Командная строка без имени exe (так её хранит движок) — дословно, минус ключ сессии `-AsdaLang=…` (кавычки и
	//   прочие ключи не трогаем: `-ExecCmds="…"` и путь проекта уходят в новый процесс как были).
	FString Args = FCommandLine::GetOriginal();
	for (int32 At = Args.Find(TEXT("-AsdaLang="), ESearchCase::IgnoreCase); At != INDEX_NONE;
		At = Args.Find(TEXT("-AsdaLang="), ESearchCase::IgnoreCase))
	{
		int32 End = At;
		while (End < Args.Len() && !FChar::IsWhitespace(Args[End])) { ++End; }
		Args.RemoveAt(At, End - At);
	}
	Args.TrimStartAndEndInline();
	const FString Exe = FPlatformProcess::ExecutablePath();
	FProcHandle H = FPlatformProcess::CreateProc(*Exe, *Args, /*bLaunchDetached*/true, /*bLaunchHidden*/false,
		/*bLaunchReallyHidden*/false, nullptr, 0, nullptr, nullptr);
	if (!H.IsValid())
	{
		UE_LOG(LogAsdaLang, Error, TEXT("[язык] перезапуск не удался: %s %s"), *Exe, *Args);
		return false;
	}
	UE_LOG(LogAsdaLang, Log, TEXT("[язык] перезапуск: %s %s"), *Exe, *Args);
	FPlatformMisc::RequestExit(false, TEXT("AsdaLang.Restart"));
	return true;
}

const TCHAR* const FAsdaLang::kCaptionTablePath = TEXT("/Game/AsdaAssets/Text/ST_GameText.ST_GameText");

int32 FAsdaLang::ApplyCaptionTable()
{
	// Редактор — только в сеансе PIE (правка в памяти с возвратом в `EndPIE`); коммандлеты — никогда.
	if (IsRunningCommandlet() || (GIsEditor && !AsdaLangPriv::bPie)) { return 0; }
	// Удержание таблицы на всю сессию (см. объявление).
	static TStrongObjectPtr<UStringTable> Held;
	UStringTable* St = Held.Get();
	if (!St)
	{
		const FString Pkg = FPackageName::ObjectPathToPackageName(FString(kCaptionTablePath));
		if (!FPackageName::DoesPackageExist(Pkg))
		{
			UE_LOG(LogAsdaLang, Log, TEXT("[язык] таблицы подписей %s нет — подписи окон из ассетов как есть"), kCaptionTablePath);
			return 0;
		}
		St = LoadObject<UStringTable>(nullptr, kCaptionTablePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!St) { return 0; }
		Held.Reset(St);
	}
#if WITH_EDITOR
	// ★ 10.10.2026 (ревизия МЕЛК-5): в PIE таблица уже изменена и не сохранена (правка владельца до PIE) — не трогать:
	//   иначе «Save All» во время PIE записал бы в ассет строки языка сессии. Подписи окон в этом сеансе — из ассета.
	if (GIsEditor && St->GetOutermost()->IsDirty())
	{
		UE_LOG(LogAsdaLang, Warning, TEXT("[язык] PIE: таблица подписей %s изменена и не сохранена — подписи окон на лету НЕ правятся (остаются строки ассета); сохраните таблицу и перезапустите PIE"), kCaptionTablePath);
		return 0;
	}
#endif
	FStringTableRef Table = St->GetMutableStringTable();
	TArray<FString> Keys;
	Table->EnumerateSourceStrings([&Keys](const FString& Key, const FString&) { Keys.Add(Key); return true; });
#if WITH_EDITORONLY_DATA
	// PIE: запомнить строки ассета ДО первой правки (возврат в `EndPIE`), с заметками — запись заменяется целиком.
	if (GIsEditor && AsdaLangPriv::PieCaptionOrig.Num() == 0)
	{
		Table->EnumerateSourceStrings([](const FString& Key, const FString& Src, const FString& Notes)
		{
			AsdaLangPriv::PieCaptionOrig.Add(Key, TPair<FString, FString>(Src, Notes));
			return true;
		});
	}
#endif
	int32 N = 0;
	for (const FString& Key : Keys)
	{
		if (!Key.IsNumeric()) { continue; }
		const FString S = FAsdaLoginData::Loc(FCString::Atoi(*Key));   // язык сессии, разметка снята (как у подписи)
		FString Cur;
		if (S.IsEmpty() || (Table->GetSourceString(FTextKey(Key), Cur) && Cur == S)) { continue; }
#if WITH_EDITORONLY_DATA
		// Сборка с данными редактора (-game и PIE): в PIE заметка записи сохраняется (вернётся вместе со строкой).
		const TPair<FString, FString>* Orig = AsdaLangPriv::PieCaptionOrig.Find(Key);
		Table->SetSourceString(FTextKey(Key), S, Orig ? Orig->Value : FString());
#else
		Table->SetSourceString(FTextKey(Key), S);
#endif
		++N;
	}
	UE_LOG(LogAsdaLang, Log, TEXT("[язык] таблица подписей окон: записей %d, обновлено под «%s» %d%s"), Keys.Num(), Code(Current()), N,
		GIsEditor ? TEXT(" (PIE, только в памяти — вернётся при остановке PIE)") : TEXT(""));
	return N;
}

void FAsdaLang::RestoreCaptionTableAfterPie()
{
#if WITH_EDITORONLY_DATA
	using namespace AsdaLangPriv;
	if (PieCaptionOrig.Num() == 0) { return; }
	UStringTable* St = FindObject<UStringTable>(nullptr, kCaptionTablePath);
	int32 N = 0;
	if (St)
	{
		FStringTableRef Table = St->GetMutableStringTable();
		for (const TPair<FString, TPair<FString, FString>>& It : PieCaptionOrig)
		{
			FString Cur;
			if (Table->GetSourceString(FTextKey(It.Key), Cur) && Cur == It.Value.Key) { continue; }
			Table->SetSourceString(FTextKey(It.Key), It.Value.Key, It.Value.Value);
			++N;
		}
	}
	UE_LOG(LogAsdaLang, Log, TEXT("[язык] PIE окончен: таблице подписей возвращены строки ассета — %d из %d"), N, PieCaptionOrig.Num());
	PieCaptionOrig.Reset();
#endif
}

bool FAsdaLang::ApplyLive(EAsdaLang L)
{
	using namespace AsdaLangPriv;
	if (IsRunningCommandlet() || !IsSelectable(L)) { return false; }
	if (!bInited) { InitAtStartup(); }
	if (Session == L) { return false; }
	const EAsdaLang Was = Session;
	Session = L;
	bOsDefault = false;
	if (!GIsEditor)
	{
		// Культура UE: новый .locres и подмена L10N-пакетов для всех СЛЕДУЮЩИХ загрузок.
		FInternationalization::Get().SetCurrentCulture(Code(L));
	}
	const int32 NCap = ApplyCaptionTable();
	// ★ 10.10.2026 (en_completeness): надписи WBP без id (полиглот; в PIE — предпросмотр языка) и картинки PIE — под новый язык.
	ApplyUiLiterals();
	ApplyPieTextures();
	OnLanguageChanged().Broadcast();
	UE_LOG(LogAsdaLang, Log, TEXT("[язык] на лету: «%s» → «%s»; культура UE «%s»; подписей обновлено %d"), Code(Was), Code(L),
		*FInternationalization::Get().GetCurrentCulture()->GetName(), NCap);
	return true;
}

FString FAsdaLang::Describe()
{
	EAsdaLang S;
	const bool bSaved = GetSaved(S);
	return FString::Printf(TEXT("сессия=%s (оригинал %d)%s, профиль=%s, культура UE=%s"),
		Code(Current()), ToOriginalCode(Current()), IsSessionOverride() ? TEXT(" [командная строка]") : TEXT(""),
		bSaved ? Code(S) : TEXT("(не выбран)"), *FInternationalization::Get().GetCurrentCulture()->GetName());
}

// ── Консоль: `asda.Lang` — состояние; `asda.Lang en|ru` — записать выбор в профиль (после перезапуска);
//    `asda.Lang.Resolve <Таблица> <Ключ>` — что вернёт оверлей (проверка ведомостей без окон). ──
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommand GAsdaLangCmd(
	TEXT("asda.Lang"),
	TEXT("[ПОРТ] Язык игры. Без аргумента — состояние; ru|en — записать в профиль (действует после перезапуска)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& A)
	{
		EAsdaLang L;
		if (A.Num() > 0 && FAsdaLang::ParseCode(A[0], L))
		{
			if (!FAsdaLang::IsSelectable(L))
			{
				UE_LOG(LogAsdaLang, Warning, TEXT("[язык] «%s» пока недоступен (решение владельца 09.10.2026)"), FAsdaLang::Code(L));
				return;
			}
			FAsdaLang::SetSaved(L, true);
		}
		UE_LOG(LogAsdaLang, Display, TEXT("[язык] %s"), *FAsdaLang::Describe());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommand GAsdaLangResolveCmd(
	TEXT("asda.Lang.Resolve"),
	TEXT("[ПОРТ] asda.Lang.Resolve <Таблица> <Ключ> — строка оверлея языка сессии (и цепочки en→ru)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& A)
	{
		if (A.Num() < 2) { UE_LOG(LogAsdaLang, Display, TEXT("asda.Lang.Resolve <Таблица> <Ключ>")); return; }
		FString S;
		const bool bHit = FAsdaLang::FindOverlay(FAsdaLang::Current(), *A[0], A[1], S);
		UE_LOG(LogAsdaLang, Display, TEXT("[язык] %s[%s] (%s): %s«%s»"), *A[0], *A[1], FAsdaLang::Code(FAsdaLang::Current()),
			bHit ? TEXT("") : TEXT("нет в оверлее, цепочка → "), *FAsdaLang::Resolve(*A[0], A[1], TEXT("<RU>")));
	}));
#endif // ASDA_DEV_CONSOLE
