// Copyright (c) 2026 @sitxovski (Discord)
#include "AsdaSessionReset.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "AsdaNetSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Subsystems/SubsystemCollection.h"   // FSubsystemCollectionBase::InitializeDependency
#include "UObject/UObjectGlobals.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "World/AsdaWorldKind.h"             // ★ F7 (дедуп №2, В3): общие признаки «логин» / «загрузка»
#include "World/AsdaCombatTrace.h"           // журнал боя: сброс сессии закрывает боевой цикл

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSession, Log, All);

const TCHAR* AsdaSessionResetReasonName(EAsdaSessionResetReason Reason)
{
	switch (Reason)
	{
	case EAsdaSessionResetReason::Disconnected: return TEXT("разрыв связи");
	case EAsdaSessionResetReason::LeftWorld:     return TEXT("выход в выбор персонажа");
	case EAsdaSessionResetReason::EnteringWorld: return TEXT("вход в мир");
	case EAsdaSessionResetReason::MapChanged:    return TEXT("смена карты");
	default:                                    return TEXT("?");
	}
}

// Служебные уровни, которые НЕ являются игровой картой: экран загрузки и карта логина.
// ★ F7 (дедуп №2, В3, 12.10.2026): свои `AsdaIsLoadingLevel/AsdaIsLoginLevel` (по имени) → общие признаки
//   `AsdaWorldKind::IsLoadingMap / IsLoginScene` (в игре «логин» = режим `AAsdaLoginGameMode`; у `/Game/Map/Login` он и только у
//   неё — признаки совпадают, сверка `asda.W3.LoginKind` в отчёте `wave3_S10.md`).

void UAsdaSessionResetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Сеть нужна прямо здесь (подписка на её делегат) — гарантируем порядок создания.
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	Super::Initialize(Collection);

	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		DisconnectHandle = Net->OnGameDisconnected.AddUObject(this, &UAsdaSessionResetSubsystem::OnGameDisconnected);
		// ★ 1021 «вход в мир»: сюда приходит выбор персонажа и НЕ приходят переходы между картами
		//   и каналами. Единственная точка, где точно известно, что начинается НОВАЯ сессия.
		EnterWorldHandle = Net->OnEnterWorld.AddUObject(this, &UAsdaSessionResetSubsystem::OnEnterWorld);
	}
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UAsdaSessionResetSubsystem::OnPostLoadMap);

	// Проверочные команды: дают прогнать оба пути сброса без сервера и без травела.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.Session.Reset"),
		TEXT("Разослать полный сброс состояния сессии (как при разрыве связи)."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			// ★ С-Р1 (26.09.2026), офлайн-гейт: проверочный сброс «разрыв связи» при живом сокете рассинхронизировал бы сессию (очереди приёма, окна, повторные 5416).
			if (FAsdaOutgoingGate::RefuseDebugWhenOnline(this, TEXT("asda.Session.Reset"))) { return; }
			Broadcast(EAsdaSessionResetReason::Disconnected);
		}),
		ECVF_Default));
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.Session.ResetMap"),
		TEXT("Разослать сброс «смена карты» (мир пересоздан, соединение пересоздано)."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			// ★ С-Р1 (26.09.2026), офлайн-гейт: проверочный сброс «смена карты» при живом сокете рассинхронизировал бы сессию.
			if (FAsdaOutgoingGate::RefuseDebugWhenOnline(this, TEXT("asda.Session.ResetMap"))) { return; }
			Broadcast(EAsdaSessionResetReason::MapChanged);
		}),
		ECVF_Default));
#endif // ASDA_DEV_CONSOLE
}

void UAsdaSessionResetSubsystem::Deinitialize()
{
	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
		PostLoadMapHandle.Reset();
	}
	if (DisconnectHandle.IsValid())
	{
		if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
		{
			Net->OnGameDisconnected.Remove(DisconnectHandle);
		}
		DisconnectHandle.Reset();
	}
	if (EnterWorldHandle.IsValid())
	{
		if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
		{
			Net->OnEnterWorld.Remove(EnterWorldHandle);
		}
		EnterWorldHandle.Reset();
	}
	for (IConsoleObject* Cmd : ConsoleCmds) { if (Cmd) { IConsoleManager::Get().UnregisterConsoleObject(Cmd); } }
	ConsoleCmds.Empty();
	Super::Deinitialize();
}

void UAsdaSessionResetSubsystem::OnGameDisconnected()
{
	// ⚠ Мы внутри колбэка закрытия соединения (закон `no-reentrant-net-callbacks`). Рассылка
	//   ничего не отправляет и не трогает сокет — только чистит поля подсистем, поэтому
	//   откладывать её не нужно и нельзя: отложенный сброс успел бы «показаться» игроку.
	Broadcast(EAsdaSessionResetReason::Disconnected);
}

void UAsdaSessionResetSubsystem::OnEnterWorld()
{
	// ⚠ Мы внутри разбора пакета 1021. Он УЖЕ записал адрес game-сервера, `EnterMapId` и канал —
	//   рассылка их не трогает (см. `UAsdaNetSubsystem::ResetSessionState`: поля перехода
	//   намеренно не чистятся). Ни одной отправки сброс не делает.
	Broadcast(EAsdaSessionResetReason::EnteringWorld);
}

void UAsdaSessionResetSubsystem::OnPostLoadMap(UWorld* LoadedWorld)
{
	if (!LoadedWorld) { return; }
	const FString Name = LoadedWorld->GetName();

	// Экран загрузки — промежуточный уровень одного и того же перехода. Ни входом, ни выходом
	// он не является: на нём ничего не сбрасываем и предыдущее состояние «где мы были» не портим.
	if (AsdaWorldKind::IsLoadingMap(LoadedWorld)) { return; }

	const bool bLogin = AsdaWorldKind::IsLoginScene(LoadedWorld);
	if (bLogin)
	{
		// Приехали в логин. Если предыдущим миром была игровая карта — это ВЫХОД В ВЫБОР ПЕРСОНАЖА.
		if (!bLastWorldWasLogin)
		{
			Broadcast(EAsdaSessionResetReason::LeftWorld);
		}
	}
	else
	{
		// Приехали на игровую карту. Из логина — это ПЕРВЫЙ вход в мир: чистить нечего, состояние
		// уже сброшено выходом (или его никогда и не было). С другой игровой карты — смена карты.
		// ⚠ Имена миров НЕ сравниваем: смена КАНАЛА грузит ТУ ЖЕ карту (`OP_NET_HANDLE_CHANNEL_TRANSFER`
		//   6015), но соединение при этом пересоздаётся `EnterGameWorld()` и сессионные номера
		//   выдаются заново — сбрасывать нужно так же, как при переходе на другую карту.
		if (!bLastWorldWasLogin)
		{
			UE_LOG(LogAsdaSession, Log, TEXT("[сессия] травел «%s» → «%s»"), *LastWorldName, *Name);
			Broadcast(EAsdaSessionResetReason::MapChanged);
		}
	}

	LastWorldName      = Name;
	bLastWorldWasLogin = bLogin;
}

void UAsdaSessionResetSubsystem::Broadcast(EAsdaSessionResetReason Reason)
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }

	// GetSubsystemArray отдаёт ВСЕ живые подсистемы GameInstance; интересуют только те,
	// что объявили о наличии состояния сессии.
	// `GetSubsystemArrayCopy` — единственная форма перечисления в UE 5.8 (прежний
	// `GetSubsystemArray`, возвращавший ссылку, из движка убран).
	const TArray<UGameInstanceSubsystem*> All = GI->GetSubsystemArrayCopy<UGameInstanceSubsystem>();
	// ⚠ ПОРЯДОК ВАЖЕН. В `UAsdaNetSubsystem` лежат зеркала полей `CCharacter` (уровень, клан,
	//   знак зодиака, ёмкость склада), и остальные подсистемы во время СВОЕГО сброса ещё читают
	//   их — например, перерисовывая опустевшие списки. Поэтому сеть обслуживаем ПОСЛЕДНЕЙ.
	UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
	ASDA_CTRACE(EAsdaCT::Exit, TEXT("сброс сессии «%s» — боевое состояние клиента обнуляется"), AsdaSessionResetReasonName(Reason));
	ASDA_CTRACE_END(TEXT("сброс сессии"));
	int32 Touched = 0;
	for (UGameInstanceSubsystem* Sub : All)
	{
		if (!Sub || Sub == this || Sub == Net) { continue; }
		if (IAsdaSessionResettable* R = Cast<IAsdaSessionResettable>(Sub))
		{
			R->ResetSessionState(Reason);
			++Touched;
		}
	}
	if (Net)
	{
		Net->ResetSessionState(Reason);
		++Touched;
	}
	UE_LOG(LogAsdaSession, Log, TEXT("[сессия] сброс «%s»: обслужено подсистем %d из %d"),
		AsdaSessionResetReasonName(Reason), Touched, All.Num());
}
