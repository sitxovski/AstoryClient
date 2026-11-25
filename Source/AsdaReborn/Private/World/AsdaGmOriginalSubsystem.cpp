// Copyright (c) 2026 @sitxovski (Discord)
// Реализация вкладки «Команды оригинала» GM-меню. Устройство и правила — в шапке `Public/World/AsdaGmOriginalSubsystem.h`,
//   раскладки и поведение сервера по каждой команде — `Reports/gm_original_commands/group1..4_spec.md`.
#include "World/AsdaGmOriginalSubsystem.h"
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "World/AsdaAdminSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaCoord.h"
#include "GameFramework/Pawn.h"
#include "Npc/AsdaMobActor.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaDisconnectPopupSubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaNoticeColors.h"
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaSoulmateSkillSubsystem.h"
#include "UI/AsdaBoosterShopSubsystem.h"
#include "UI/AsdaBoosterPanelSubsystem.h"
#include "UI/AsdaPremiumShopSubsystem.h"
#include "UI/AsdaHairShopSubsystem.h"
#include "UI/AsdaSystemMenuSubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "World/AsdaFadeComponent.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWorldKind.h"
#include "World/AsdaFloatingTextSubsystem.h"
#include "Engine/GameViewportClient.h"
#include "Login/AsdaText.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/DateTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaGmOrigUi, Log, All);

#include "Npc/AsdaNpcGmOnly.h"   // ★ 09.11.2026 GM-телепорт: имя «GM_…» своего персонажа (жёсткий префикс, не настройка)
#include "HAL/IConsoleManager.h"

// ★ 09.11.2026 [ПОРТ] GM-телепорт Ctrl+ЛКМ по карте (шапка `UAsdaGmOriginalSubsystem::TryMapClickTeleport`). Ключ только ВЫКЛЮЧАЕТ:
//   включить его не-GM нельзя — право проверяется по имени с сервера и по ответу сервера, не по ключу.
static TAutoConsoleVariable<int32> CVarAsdaGmMapClickTeleport(
	TEXT("asda.Gm.MapClickTeleport"), 1,
	TEXT("1 — GM (имя GM_ + права от сервера) Ctrl+ЛКМ по мини-карте/карте мира переносится в клетку штатной 6010; 0 — выключено, Ctrl+клик как обычный клик."),
	ECVF_Default);

// ★ 09.11.2026 [ПОРТ, P123] чужая карта — 6010 с картой (сервер P123). 0 — прежнее: на чужую карту ничего не шлётся (строка 991300).
static TAutoConsoleVariable<int32> CVarAsdaGmMapTeleportOtherMap(
	TEXT("asda.Gm.MapTeleportOtherMap"), 1,
	TEXT("1 — GM Ctrl+ЛКМ по чужой карте на карте мира шлёт 6010 с картой (серверный P123 → 5055 переход); 0 — ничего не шлёт."),
	ECVF_Default);

namespace AsdaGmMapTp
{
	// Строки (RU — gametext_overrides.json, EN — loc/en/GameText.json).
	constexpr int32 kOtherMap = 991300;   // чужая карта при ключе asda.Gm.MapTeleportOtherMap 0: ничего не отправлено
	constexpr int32 kNoRights = 991301;   // права оператора сервером не подтверждены
	constexpr int32 kOutOfMap = 991302;   // клетка [ss1], [ss2] вне карты
	constexpr int32 kJumpName = 990804;   // «Прыжок в клетку» — та же подпись, что у пункта GM-меню
	constexpr int32 kJumpMap  = 991303;   // «Прыжок на карту [ss1] → [ss2], [ss3]» (P123)
	constexpr double kProbeWaitSec = 5.0; // ожидание ответа 6037 на пробу прав
}

// ★ 29.10.2026 (ключ asda.Map.DestMarkClear, разбор — в AsdaMinimapSubsystem.cpp): снять метку цели автобега на картах.
void AsdaDestMarkClear(UGameInstance* GI, const TCHAR* Why);

namespace AsdaGmOrigUi
{
	// Цвет ARGB эталона (int32 со знаком, как в `CChat_AddTokenizedLines`) → FColor.
	static FColor Argb(int32 V)
	{
		const uint32 U = (uint32)V;
		return FColor((uint8)(U >> 16), (uint8)(U >> 8), (uint8)U, (uint8)(U >> 24));
	}
	const int32 kRed = -65536;          // нотисы 2113/2116/2118 цели (group1 §4.3, §5.3, §6.3)
	const int32 kGold = -11514;         // плашка итогов 6039/6043 (group3 §1.5)
	const int32 kChatGm = -15132161;    // строка чата итогов 6039/6043 (group3 §1.5)
	const int32 kBlue = -16776961;      // печать 6075 (group3 §4.3)
	const int32 kTtl = 10000;           // время жизни строк, мс (эталон 10000)

	// loc-строки вкладки (DT_GameText, RU — `Tools/data/gametext_overrides.json`, EN — `Tools/data/loc/en/GameText.json`).
	constexpr int32 kNeedPlayer = 990790, kNeedMob = 990791, kSentNoReply = 990792, kNotSent = 990793, kBadInput = 990794;
	constexpr int32 kStatsUnreliable = 990795, kInvToggled = 990796, kNameTitle = 990760, kNameGuide = 990761;
	constexpr int32 kCellTitle = 990762, kCellGuide = 990763, kEventTitle = 990764, kEventGuide = 990765;
	constexpr int32 kEventOffTitle = 990766, kEventOffGuide = 990767, kGiveTitle = 990768, kGiveGuide = 990769;
	constexpr int32 kGroupTitle = 990770, kGroupGuide = 990771, kConfirmKick = 990780, kConfirmKill = 990781;
	constexpr int32 kConfirmMobKill = 990782;
	constexpr int32 kNeedPlayerOrMob = 990798;   // «Выберите целью игрока или монстра» (6075)
	// Группа 4: окна ввода и подтверждения.
	constexpr int32 kMapTitle = 990820, kMapGuide = 990821, kCountTitle = 990822, kCountGuide = 990823;
	constexpr int32 kAreaTitle = 990824, kAreaGuide = 990825, kConfirmCallMap = 990830, kConfirmClearItems = 990831;
	constexpr int32 kConfirmArea = 990832, kConfirmBlockOn = 990833, kConfirmBlockOff = 990834;
	// Нотисы оригинала у цели: 2113 «Тебя отключила Служба Поддержки.», 2116 «Служба Поддержки убивает тебя»,
	//   2118 «Служба Поддержки переместила тебя».
	constexpr int32 kNoticeKicked = 2113, kNoticeKilled = 2116, kNoticeMoved = 2118;
	// Подписи 6037 (`chatDumpCharacterStats@0xF4B260`): 11029 «Уровень», 11039 «Опыт», 2304 «Золото», 12038…12043 характеристики.
	constexpr int32 kLocLevel = 11029, kLocExp = 11039, kLocGold = 2304, kLocStat0 = 12038;

	// Итоги 6043 оператору — литералы эталона `ItemPop_ShowTransferResult@0xF4BE30` (group3 §3.3), не loc.
	static const TCHAR* ItemPopText(int32 Code)
	{
		switch (Code)
		{
			case 0: return TEXT("[ SUCCESS ITEM POP ]");
			case 1: return TEXT("[ FAil ITEM POP ]");
			case 2: return TEXT("[ TARGET USER INVEN LOW ]");
			case 3: return TEXT("[ TARGET USER BAG LOW ]");
			case 4: return TEXT("[ WRONG ITEM CODE ]");
			case 5: return TEXT("[ WRONG USER INFO ]");
			case 6: return TEXT("[ TARGET USER WEIGHT OVER ]");
			case 7: return TEXT("[ ITEM AMOUT ZERO ]");
			default: return nullptr;
		}
	}

	// Слова строки ввода (через пробелы/запятые/«;»).
	static TArray<FString> Words(const FString& S)
	{
		FString T = S;
		T.ReplaceInline(TEXT(","), TEXT(" "));
		T.ReplaceInline(TEXT(";"), TEXT(" "));
		TArray<FString> W;
		T.ParseIntoArrayWS(W);
		return W;
	}
	// Целое из слова (со знаком). false — не целое.
	static bool Int(const FString& S, int32& Out)
	{
		if (S.IsEmpty() || !(S.IsNumeric() || (S.StartsWith(TEXT("-")) && S.Mid(1).IsNumeric())) || S.Contains(TEXT("."))) { return false; }
		Out = FCString::Atoi(*S);
		return true;
	}
	// Дробное число из слова («2», «2.5», «2,5»). false — не число.
	static bool Float(const FString& S, float& Out)
	{
		const FString T = S.Replace(TEXT(","), TEXT("."));
		int32 Dots = 0;
		for (const TCHAR Ch : T) { if (Ch == TEXT('.')) { ++Dots; } else if (!FChar::IsDigit(Ch)) { return false; } }
		if (T.IsEmpty() || Dots > 1 || T == TEXT(".")) { return false; }
		Out = FCString::Atof(*T);
		return true;
	}
	// «ДД.ММ» → день, месяц. false — не в формате.
	static bool DayMon(const FString& S, uint8& D, uint8& M)
	{
		FString L, R;
		int32 a = 0, b = 0;
		if (!S.Split(TEXT("."), &L, &R) || !Int(L, a) || !Int(R, b) || a < 1 || a > 31 || b < 1 || b > 12) { return false; }
		D = (uint8)a; M = (uint8)b;
		return true;
	}
	// «ЧЧ:ММ» → час, минута (сервер допускает 0…24 и 0…60, group3 §1.3). false — не в формате.
	static bool HourMin(const FString& S, uint8& H, uint8& M)
	{
		FString L, R;
		int32 a = 0, b = 0;
		if (!S.Split(TEXT(":"), &L, &R) || !Int(L, a) || !Int(R, b) || a < 0 || a > 24 || b < 0 || b > 60) { return false; }
		H = (uint8)a; M = (uint8)b;
		return true;
	}
}

// ═════════ Жизненный цикл ═════════════════════════════════════════════════════════════════════════════════

void UAsdaGmOriginalSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaAdminSubsystem::StaticClass());   // реестр GM-меню должен быть поднят
	if (UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
	{
		HGmPacket = Net->OnGmOriginalPacket.AddUObject(this, &UAsdaGmOriginalSubsystem::OnGmPacket);
	}
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UAsdaGmOriginalSubsystem::OnTick), 0.f);
	RegisterCommands();
}

void UAsdaGmOriginalSubsystem::Deinitialize()
{
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		if (HGmPacket.IsValid()) { Net->OnGmOriginalPacket.Remove(HGmPacket); }
	}
	HGmPacket.Reset();
	if (TickHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); }
	TickHandle.Reset();
	Queue.Reset();
	Super::Deinitialize();
}

FString UAsdaGmOriginalSubsystem::Loc(int32 Id) const
{
	return FAsdaText::Loc(Id);
}

void UAsdaGmOriginalSubsystem::AddLine(const FString& Text)
{
	LastLines.Add(Text);
	if (LastLines.Num() > 64) { LastLines.RemoveAt(0, LastLines.Num() - 64); }
}

void UAsdaGmOriginalSubsystem::Say(const FString& Text, FColor Color, bool bPlank)
{
	AddLine(Text);
	const UGameInstance* GI = GetGameInstance();
	if (bPlank) { AsdaNotice::Plank(GI, EAsdaMsgCategory::System, Text, Color, (float)AsdaGmOrigUi::kTtl); }
	AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, Text, Color, (float)AsdaGmOrigUi::kTtl);
	UE_LOG(LogAsdaGmOrigUi, Log, TEXT("[GM-ориг] чат: %s"), *Text);
}

void UAsdaGmOriginalSubsystem::SaySent(bool bOk, const FString& What)
{
	// Сервер на эти команды не отвечает ни при успехе, ни при отказе гейта (group1 §11, group2 §9) — честно так и пишем.
	Say(FString::Printf(TEXT("%s: %s"), *What, *Loc(bOk ? AsdaGmOrigUi::kSentNoReply : AsdaGmOrigUi::kNotSent)),
		FColor(0xE9, 0xD6, 0xBC));
}

// ═════════ Регистрация пунктов ════════════════════════════════════════════════════════════════════════════

void UAsdaGmOriginalSubsystem::AddCommand(const TCHAR* Id, int32 TitleLoc, int32 HintLoc, TFunction<void(APlayerController*)> Activate)
{
	UAsdaAdminSubsystem* Admin = GetGameInstance()->GetSubsystem<UAsdaAdminSubsystem>();
	if (!Admin) { return; }
	const FName N(Id);
	Admin->RegisterFunction(N, FText::FromString(Loc(TitleLoc)), FText::FromString(Loc(HintLoc)), MoveTemp(Activate));
	Admin->SetFunctionLoc(N, TitleLoc, HintLoc);
	Admin->SetFunctionTab(N, EAsdaAdminTab::Original);
}

void UAsdaGmOriginalSubsystem::RegisterCommands()
{
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	if (!Net) { return; }
	TWeakObjectPtr<UAsdaGmOriginalSubsystem> Self(this);
	TWeakObjectPtr<UAsdaNetSubsystem> WNet(Net);

	// По имени — 6012/6014/6018 (поиск через RelayServer, любой сервер).
	AddCommand(TEXT("GMO_SUMMON"), 990710, 990711, [Self](APlayerController*) { if (Self.IsValid()) { Self->ActByName(0); } });
	AddCommand(TEXT("GMO_MEET"),   990712, 990713, [Self](APlayerController*) { if (Self.IsValid()) { Self->ActByName(1); } });
	AddCommand(TEXT("GMO_KICK"),   990714, 990715, [Self](APlayerController*) { if (Self.IsValid()) { Self->ActByName(2); } });
	// Выбранная цель этого GameServer — 6022/6026.
	AddCommand(TEXT("GMO_KILL"),   990716, 990717, [Self](APlayerController* PC) { if (Self.IsValid()) { Self->ActKill(PC); } });
	AddCommand(TEXT("GMO_MOVE"),   990718, 990719, [Self](APlayerController* PC) { if (Self.IsValid()) { Self->ActMove(PC); } });
	// Объявления — 6028/6029 (текст — файл сервера).
	AddCommand(TEXT("GMO_LOCALCHAT"), 990720, 990721, [Self, WNet](APlayerController*)
	{
		if (Self.IsValid() && WNet.IsValid()) { Self->SaySent(WNet->SendGmLocalChat6028(), Self->Loc(990720)); }
	});
	AddCommand(TEXT("GMO_GLOBALCHAT"), 990722, 990723, [Self, WNet](APlayerController*)
	{
		if (Self.IsValid() && WNet.IsValid()) { Self->SaySent(WNet->SendGmGlobalChat6029(), Self->Loc(990722)); }
	});
	// Себе — 6032 (опыт / золото / очки навыков).
	AddCommand(TEXT("GMO_ADD_EXP"),   990724, 990725, [Self](APlayerController*) { if (Self.IsValid()) { Self->ActAdd(0); } });
	AddCommand(TEXT("GMO_ADD_GOLD"),  990726, 990727, [Self](APlayerController*) { if (Self.IsValid()) { Self->ActAdd(1); } });
	AddCommand(TEXT("GMO_ADD_SKILL"), 990728, 990729, [Self](APlayerController*) { if (Self.IsValid()) { Self->ActAdd(2); } });
	// Монстр — 6033 (карточка; убить с карточкой), 6045/6046 (группы).
	AddCommand(TEXT("GMO_MOBINFO"),   990730, 990731, [Self](APlayerController* PC) { if (Self.IsValid()) { Self->ActMobInfo(PC, 0); } });
	AddCommand(TEXT("GMO_MOBKILL"),   990748, 990749, [Self](APlayerController* PC) { if (Self.IsValid()) { Self->ActMobInfo(PC, 1); } });
	AddCommand(TEXT("GMO_MOBPOP"),    990736, 990737, [Self](APlayerController* PC) { if (Self.IsValid()) { Self->ActMonsterGroup(PC, true); } });
	AddCommand(TEXT("GMO_MOBRETURN"), 990738, 990739, [Self](APlayerController* PC) { if (Self.IsValid()) { Self->ActMonsterGroup(PC, false); } });
	// Себе — 6035 неуязвимость, 6036 характеристики.
	AddCommand(TEXT("GMO_INVINCIBLE"), 990732, 990733, [Self, WNet](APlayerController*)
	{
		if (!Self.IsValid() || !WNet.IsValid()) { return; }
		const bool bOk = WNet->SendGmInvincible6035();
		if (bOk) { ++Self->InvincibleToggles; }
		Self->Say(FString::Printf(TEXT("%s (%d)"), *Self->Loc(bOk ? AsdaGmOrigUi::kInvToggled : AsdaGmOrigUi::kNotSent), Self->InvincibleToggles),
			FColor(0xE9, 0xD6, 0xBC));
	});
	AddCommand(TEXT("GMO_STATS"), 990734, 990735, [Self, WNet](APlayerController*)
	{
		if (!Self.IsValid() || !WNet.IsValid()) { return; }
		Self->bStatsPending = WNet->SendGmStats6036();
		if (!Self->bStatsPending) { Self->SaySent(false, Self->Loc(990734)); }
	});
	// Ивенты — 6038/6040.
	AddCommand(TEXT("GMO_EVENTSET"), 990740, 990741, [Self](APlayerController*) { if (Self.IsValid()) { Self->ActEventSet(); } });
	AddCommand(TEXT("GMO_EVENTOFF"), 990742, 990743, [Self](APlayerController*) { if (Self.IsValid()) { Self->ActEventOff(); } });
	// Выбранная цель — 6042 выдать предмет, 6075 проценты против цели.
	AddCommand(TEXT("GMO_ITEMGIVE"),  990744, 990745, [Self](APlayerController* PC) { if (Self.IsValid()) { Self->ActItemGive(PC); } });
	AddCommand(TEXT("GMO_CHARVALUE"), 990746, 990747, [Self](APlayerController* PC) { if (Self.IsValid()) { Self->ActCharValue(PC); } });
	// Группа 4 (`group4_spec.md` §11): карта, сектор, прыжок, воскрешение, откаты, отсчёт, область, блок quizstage.
	AddCommand(TEXT("GMO_CALLMAP"),    990800, 990801, [Self](APlayerController*) { if (Self.IsValid()) { Self->ActCallMap(); } });
	AddCommand(TEXT("GMO_CLEARITEMS"), 990802, 990803, [Self](APlayerController*) { if (Self.IsValid()) { Self->ActClearItems(); } });
	AddCommand(TEXT("GMO_JUMP"),       990804, 990805, [Self](APlayerController* PC) { if (Self.IsValid()) { Self->ActJump(PC); } });
	AddCommand(TEXT("GMO_REVIVE"),     990806, 990807, [Self](APlayerController* PC) { if (Self.IsValid()) { Self->ActRevive(PC); } });
	AddCommand(TEXT("GMO_COOLRESET"),  990808, 990809, [Self](APlayerController* PC) { if (Self.IsValid()) { Self->ActCoolReset(PC); } });
	AddCommand(TEXT("GMO_COUNTDOWN"),  990810, 990811, [Self](APlayerController*) { if (Self.IsValid()) { Self->ActCountDown(); } });
	AddCommand(TEXT("GMO_AREAJUMP"),   990812, 990813, [Self](APlayerController* PC) { if (Self.IsValid()) { Self->ActAreaJump(PC); } });
	AddCommand(TEXT("GMO_MAPBLOCK_ON"),  990814, 990815, [Self](APlayerController*) { if (Self.IsValid()) { Self->ActMapBlock(true); } });
	AddCommand(TEXT("GMO_MAPBLOCK_OFF"), 990816, 990817, [Self](APlayerController*) { if (Self.IsValid()) { Self->ActMapBlock(false); } });
}

// ═════════ Цель ═══════════════════════════════════════════════════════════════════════════════════════════

bool UAsdaGmOriginalSubsystem::GetTargetPlayer(APlayerController* PC, int32& OutUserNum, int32& OutUserIndex, FString& OutName,
	bool bAllowSelf)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
	UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
	const AAsdaPlayerController* APC = Cast<AAsdaPlayerController>(PC);
	const AActor* Target = APC ? APC->TargetActor.Get() : nullptr;
	OutUserNum = (Target && WS) ? WS->FindUserIdxByActor(Target) : -1;
	int32 Cls = 0, Lvl = 0, Clan = -1;
	if (OutUserNum >= 0 && Net && Net->IsSelfUserIdx(OutUserNum))
	{
		if (!bAllowSelf) { Say(Loc(AsdaGmOrigUi::kNeedPlayer), FColor(0xE9, 0xD6, 0xBC)); return false; }
		OutUserIndex = Net->GetSelfCharUserIndex();   // своё `CUser+0x174` (ревизия С-2)
		OutName = TEXT("(я)");
		return true;
	}
	// ★ Ревизия М-7: выбран НЕ игрок (монстр, NPC) — это не «без цели»; молча уходить на себя нельзя.
	if (Target && OutUserNum < 0)
	{
		Say(Loc(AsdaGmOrigUi::kNeedPlayer), FColor(0xE9, 0xD6, 0xBC));
		return false;
	}
	if (OutUserNum < 0 || !WS || !WS->GetNetPlayerInfo(OutUserNum, OutName, Cls, Lvl, Clan))
	{
		// Цели нет — себе, если команда это допускает (выдача предмета/проценты себе — штатно у сервера).
		if (bAllowSelf && Net && Net->bSelfUserNumKnown)
		{
			OutUserNum = (int32)Net->SelfUserNum;
			OutUserIndex = Net->GetSelfCharUserIndex();
			OutName = TEXT("(я)");
			return true;
		}
		Say(Loc(AsdaGmOrigUi::kNeedPlayer), FColor(0xE9, 0xD6, 0xBC));
		return false;
	}
	OutUserIndex = WS->GetNetPlayerCharId(OutUserNum);   // = CUser+0x174 цели (group1 §1.5), им сервер сверяет «тот ли это»
	return true;
}

bool UAsdaGmOriginalSubsystem::GetTargetMob(APlayerController* PC, int32& OutRid, int32& OutGroup)
{
	const AAsdaPlayerController* APC = Cast<AAsdaPlayerController>(PC);
	const AAsdaMobActor* M = APC ? Cast<AAsdaMobActor>(APC->TargetActor.Get()) : nullptr;
	if (!M || M->NetRenderId < 0)
	{
		OutRid = -1; OutGroup = -1;
		return false;
	}
	OutRid = M->NetRenderId;   // номер сущности сервера (4013) = индекс моба NpcServer (group2 §4.1, group3 §4.1)
	OutGroup = M->GroupId;
	return true;
}

// ═════════ Действия ═══════════════════════════════════════════════════════════════════════════════════════

void UAsdaGmOriginalSubsystem::ActByName(int32 Which)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
	UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
	if (!WM) { return; }
	// Имя по умолчанию — выбранный игрок (если есть), чтобы не набирать.
	FString Initial;
	if (APlayerController* PC = GI->GetFirstLocalPlayerController(GI->GetWorld()))
	{
		const AAsdaPlayerController* APC = Cast<AAsdaPlayerController>(PC);
		const int32 Uid = (APC && APC->TargetActor.IsValid() && WS) ? WS->FindUserIdxByActor(APC->TargetActor.Get()) : -1;
		int32 C = 0, L = 0, Cl = -1;
		if (Uid >= 0 && WS) { WS->GetNetPlayerInfo(Uid, Initial, C, L, Cl); }
	}
	const int32 TitleLoc = (Which == 0) ? 990710 : (Which == 1) ? 990712 : 990714;
	TWeakObjectPtr<UAsdaGmOriginalSubsystem> Self(this);
	WM->ShowInputDialog(Loc(TitleLoc), Loc(AsdaGmOrigUi::kNameGuide), Initial, 19, [Self, Which, TitleLoc](bool bOk, const FString& Name)
	{
		UAsdaGmOriginalSubsystem* S = Self.Get();
		if (!S || !bOk || Name.TrimStartAndEnd().IsEmpty()) { return; }
		UAsdaNetSubsystem* Net = S->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
		UAsdaWindowSubsystem* WM2 = S->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
		if (!Net) { return; }
		const FString N = Name.TrimStartAndEnd();
		if (Which == 2 && WM2)
		{
			// Кик — подтверждение (цель потеряет связь).
			AsdaEnterGuard::NoEnter(WM2)->ShowGameConfirm(FAsdaText::Loc(AsdaGmOrigUi::kConfirmKick).Replace(TEXT("[ss1]"), *N), [Self, N, TitleLoc](bool bYes)
			{
				UAsdaGmOriginalSubsystem* S2 = Self.Get();
				UAsdaNetSubsystem* Net2 = S2 ? S2->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
				if (bYes && S2 && Net2) { S2->SaySent(Net2->SendGmKick6018(N), S2->Loc(TitleLoc) + TEXT(" «") + N + TEXT("»")); }
			});
			return;
		}
		const bool bSent = (Which == 0) ? Net->SendGmSummon6012(N) : Net->SendGmMeet6014(N);
		S->SaySent(bSent, S->Loc(TitleLoc) + TEXT(" «") + N + TEXT("»"));
	});
}

void UAsdaGmOriginalSubsystem::ActKill(APlayerController* PC)
{
	int32 Uid = -1, UIdx = -1; FString Name;
	if (!GetTargetPlayer(PC, Uid, UIdx, Name, /*bAllowSelf*/false)) { return; }
	UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
	if (!WM) { return; }
	TWeakObjectPtr<UAsdaGmOriginalSubsystem> Self(this);
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Loc(AsdaGmOrigUi::kConfirmKill).Replace(TEXT("[ss1]"), *Name), [Self, Uid, UIdx, Name](bool bYes)
	{
		UAsdaGmOriginalSubsystem* S = Self.Get();
		UAsdaNetSubsystem* Net = S ? S->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (bYes && S && Net) { S->SaySent(Net->SendGmKill6022(Uid, UIdx), S->Loc(990716) + TEXT(" «") + Name + TEXT("»")); }
	});
}

void UAsdaGmOriginalSubsystem::ActMove(APlayerController* PC)
{
	int32 Uid = -1, UIdx = -1; FString Name;
	if (!GetTargetPlayer(PC, Uid, UIdx, Name, /*bAllowSelf*/false)) { return; }
	UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
	if (!WM) { return; }
	TWeakObjectPtr<UAsdaGmOriginalSubsystem> Self(this);
	WM->ShowInputDialog(Loc(AsdaGmOrigUi::kCellTitle), Loc(AsdaGmOrigUi::kCellGuide), FString(), 20,
		[Self, Uid, UIdx, Name](bool bOk, const FString& Text)
	{
		UAsdaGmOriginalSubsystem* S = Self.Get();
		UAsdaNetSubsystem* Net = S ? S->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!bOk || !S || !Net) { return; }
		int32 X = 0, Y = 0;
		if (!ParseCell(Text, X, Y)) { S->Say(FString::Printf(TEXT("%s: %s"), *S->Loc(AsdaGmOrigUi::kBadInput), *Text), FColor(0xE9, 0xD6, 0xBC)); return; }
		// Режим 0 = перенос (режим 1 у оригинала по ошибке останавливает самого GM — не выносим, group1 §6.2).
		S->SaySent(Net->SendGmMove6026(0, X, Y, Uid, UIdx), FString::Printf(TEXT("%s «%s» → %d, %d"), *S->Loc(990718), *Name, X, Y));
	});
}

void UAsdaGmOriginalSubsystem::ActAdd(uint8 Type)
{
	UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
	if (!WM) { return; }
	// Пределы: опыт и золото — i32 (сервер знак не проверяет, вводим только положительное); очки навыков — младшее слово.
	const int32 Max = (Type == 2) ? 32767 : 2000000000;
	TWeakObjectPtr<UAsdaGmOriginalSubsystem> Self(this);
	WM->ShowNumPad(Max, [Self, Type](bool bOk, int32 Value)
	{
		UAsdaGmOriginalSubsystem* S = Self.Get();
		UAsdaNetSubsystem* Net = S ? S->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!bOk || Value <= 0 || !S || !Net) { return; }
		const int32 TitleLoc = (Type == 0) ? 990724 : (Type == 1) ? 990726 : 990728;
		const bool bSent = Net->SendGmAdd6032(Type, Value);
		// Ответ есть (4038/4036 опыт, 5013 золото, 5254 очки) — их обрабатывают штатные приёмники; здесь только факт.
		S->Say(FString::Printf(TEXT("%s +%d: %s"), *S->Loc(TitleLoc), Value, *S->Loc(bSent ? 990797 : AsdaGmOrigUi::kNotSent)),
			FColor(0xE9, 0xD6, 0xBC));
	}, /*bCenterOnScreen*/true);
}

void UAsdaGmOriginalSubsystem::ActMobInfo(APlayerController* PC, uint8 Mode)
{
	int32 Rid = -1, Group = -1;
	if (!GetTargetMob(PC, Rid, Group)) { Say(Loc(AsdaGmOrigUi::kNeedMob), FColor(0xE9, 0xD6, 0xBC)); return; }
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	if (!Net) { return; }
	if (Mode == 0)
	{
		const bool bSent = Net->SendGmMobInfo6033(0, Rid);
		if (!bSent) { SaySent(false, Loc(990730)); }
		return;   // ответ 6034 — карточка в чат
	}
	// Режим ≠0 убивает монстра (Process_MonsterDead) и присылает карточку — подтверждение.
	UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
	if (!WM) { return; }
	TWeakObjectPtr<UAsdaGmOriginalSubsystem> Self(this);
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Loc(AsdaGmOrigUi::kConfirmMobKill), [Self, Rid, Mode](bool bYes)
	{
		UAsdaGmOriginalSubsystem* S = Self.Get();
		UAsdaNetSubsystem* Net2 = S ? S->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (bYes && S && Net2 && !Net2->SendGmMobInfo6033(Mode, Rid)) { S->SaySent(false, S->Loc(990748)); }
	});
}

void UAsdaGmOriginalSubsystem::ActMonsterGroup(APlayerController* PC, bool bPop)
{
	UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
	if (!WM) { return; }
	int32 Rid = -1, Group = -1;
	GetTargetMob(PC, Rid, Group);   // группа выбранного монстра — по умолчанию
	const FString Initial = (Group >= 0) ? FString::Printf(TEXT("%d%s"), Group, bPop ? TEXT(" 0") : TEXT("")) : FString();
	TWeakObjectPtr<UAsdaGmOriginalSubsystem> Self(this);
	WM->ShowInputDialog(Loc(AsdaGmOrigUi::kGroupTitle), Loc(AsdaGmOrigUi::kGroupGuide), Initial, 20, [Self, bPop](bool bOk, const FString& Text)
	{
		UAsdaGmOriginalSubsystem* S = Self.Get();
		UAsdaNetSubsystem* Net = S ? S->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!bOk || !S || !Net) { return; }
		int32 G = 0, M = 0;
		if (!ParseGroup(Text, G, M)) { S->Say(FString::Printf(TEXT("%s: %s"), *S->Loc(AsdaGmOrigUi::kBadInput), *Text), FColor(0xE9, 0xD6, 0xBC)); return; }
		const bool bSent = bPop ? Net->SendGmMonsterPop6045(G, (uint8)M) : Net->SendGmMonsterReturn6046(G);
		S->SaySent(bSent, FString::Printf(TEXT("%s %d"), *S->Loc(bPop ? 990736 : 990738), G));
	});
}

void UAsdaGmOriginalSubsystem::ActEventSet()
{
	UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
	if (!WM) { return; }
	TWeakObjectPtr<UAsdaGmOriginalSubsystem> Self(this);
	WM->ShowInputDialog(Loc(AsdaGmOrigUi::kEventTitle), Loc(AsdaGmOrigUi::kEventGuide), DefaultEventSetText(), 66, [Self](bool bOk, const FString& Text)
	{
		UAsdaGmOriginalSubsystem* S = Self.Get();
		UAsdaNetSubsystem* Net = S ? S->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!bOk || !S || !Net) { return; }
		FEventSetArgs A;
		if (!ParseEventSet(Text, A)) { S->Say(FString::Printf(TEXT("%s: %s"), *S->Loc(AsdaGmOrigUi::kBadInput), *Text), FColor(0xE9, 0xD6, 0xBC)); return; }
		const bool bSent = Net->SendGmEventSet6038(A.Type, A.Mult, A.Channel, A.Map, A.SMon, A.SDay, A.SHour, A.SMin,
			A.EMon, A.EDay, A.EHour, A.EMin, A.MinLevel, A.MaxLevel);
		if (!bSent) { S->SaySent(false, S->Loc(990740)); }   // ответ 6039 печатает штатный приёмник
	});
}

void UAsdaGmOriginalSubsystem::ActEventOff()
{
	UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
	if (!WM) { return; }
	TWeakObjectPtr<UAsdaGmOriginalSubsystem> Self(this);
	WM->ShowInputDialog(Loc(AsdaGmOrigUi::kEventOffTitle), Loc(AsdaGmOrigUi::kEventOffGuide), TEXT("1 -1 -1"), 30, [Self](bool bOk, const FString& Text)
	{
		UAsdaGmOriginalSubsystem* S = Self.Get();
		UAsdaNetSubsystem* Net = S ? S->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!bOk || !S || !Net) { return; }
		uint8 T = 0; int32 Ch = -1, Map = -1;
		if (!ParseEventOff(Text, T, Ch, Map)) { S->Say(FString::Printf(TEXT("%s: %s"), *S->Loc(AsdaGmOrigUi::kBadInput), *Text), FColor(0xE9, 0xD6, 0xBC)); return; }
		if (!Net->SendGmEventOff6040(T, Ch, Map)) { S->SaySent(false, S->Loc(990742)); }   // ответ 6041 — штатный приёмник
	});
}

void UAsdaGmOriginalSubsystem::ActItemGive(APlayerController* PC)
{
	int32 Uid = -1, UIdx = -1; FString Name;
	if (!GetTargetPlayer(PC, Uid, UIdx, Name, /*bAllowSelf*/true)) { return; }
	UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
	if (!WM) { return; }
	TWeakObjectPtr<UAsdaGmOriginalSubsystem> Self(this);
	WM->ShowInputDialog(Loc(AsdaGmOrigUi::kGiveTitle) + TEXT(": ") + Name, Loc(AsdaGmOrigUi::kGiveGuide), FString(), 30,
		[Self, Uid, UIdx](bool bOk, const FString& Text)
	{
		UAsdaGmOriginalSubsystem* S = Self.Get();
		UAsdaNetSubsystem* Net = S ? S->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!bOk || !S || !Net) { return; }
		int32 Code = 0, Amount = 0; bool bWh = false;
		if (!ParseItemGive(Text, Code, Amount, bWh)) { S->Say(FString::Printf(TEXT("%s: %s"), *S->Loc(AsdaGmOrigUi::kBadInput), *Text), FColor(0xE9, 0xD6, 0xBC)); return; }
		if (!Net->SendGmItemGive6042(Uid, UIdx, Code, Amount, bWh)) { S->SaySent(false, S->Loc(990744)); }   // итог — 6043
	});
}

void UAsdaGmOriginalSubsystem::ActCharValue(APlayerController* PC)
{
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	if (!Net) { return; }
	int32 Rid = -1, Group = -1;
	bool bSent = false;
	if (GetTargetMob(PC, Rid, Group))
	{
		bSent = Net->SendGmCharValue6075(0, Rid);   // монстр
	}
	else
	{
		const AAsdaPlayerController* APC = Cast<AAsdaPlayerController>(PC);
		if (!APC || !APC->TargetActor.IsValid()) { Say(Loc(AsdaGmOrigUi::kNeedPlayerOrMob), FColor(0xE9, 0xD6, 0xBC)); return; }   // ревизия М-8
		int32 Uid = -1, UIdx = -1; FString Name;
		if (!GetTargetPlayer(PC, Uid, UIdx, Name, /*bAllowSelf*/false)) { return; }
		bSent = Net->SendGmCharValue6075(1, Uid);   // игрок
	}
	if (!bSent) { SaySent(false, Loc(990746)); }
}

// ═════════ Ответы ═════════════════════════════════════════════════════════════════════════════════════════

void UAsdaGmOriginalSubsystem::OnGmPacket(int32 Opcode, const TArray<uint8>& Body)
{
	Queue.Emplace(Opcode, Body);   // разбор — в тике (закон no-reentrant-net-callbacks)
}

void UAsdaGmOriginalSubsystem::DebugInjectReply(int32 Opcode, const TArray<uint8>& Body)
{
	OnGmPacket(Opcode, Body);
}

bool UAsdaGmOriginalSubsystem::OnTick(float DeltaTime)
{
	if (Queue.Num() > 0) { ProcessQueue(); }
	if (bTpPending) { TickMapTpPending(); }
	// Отсчёт 6145 тикает только в мире (у эталона тик в состоянии 1 главного цикла; на загрузке и на сцене входа стоит).
	if (bCdRunning)
	{
		const UGameInstance* GI = GetGameInstance();
		const UWorld* W = GI ? GI->GetWorld() : nullptr;
		if (W && !AsdaWorldKind::IsLoginScene(W) && SelfChar()) { TickCountDown(DeltaTime * 1000.f); }
	}
	return true;
}

void UAsdaGmOriginalSubsystem::ProcessQueue()
{
	TArray<TPair<int32, TArray<uint8>>> Batch = MoveTemp(Queue);
	Queue.Reset();
	for (const TPair<int32, TArray<uint8>>& P : Batch)
	{
		HandleReply(P.Key, P.Value);
		// ★ Ревизия М-1: кик 6019 закрыл связь и сбросил сессию — хвост пачки (6145/6147 того же кадра) уже чужой.
		if (P.Key == 6019) { Queue.Reset(); break; }
	}
}

void UAsdaGmOriginalSubsystem::HandleReply(int32 Opcode, const TArray<uint8>& B)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
	switch (Opcode)
	{
		case 6019:   // кик оператором: эталон `Show_DisconnectPopup(2113)` — закрыть свой сокет, окно 8000 и строка чата 2113
		{
			++KickCount;
			Say(Loc(AsdaGmOrigUi::kNoticeKicked), AsdaGmOrigUi::Argb(AsdaGmOrigUi::kRed));
			if (Net && Net->IsConnected())
			{
				// ★ Ревизия С-1: текст попапа выставляется ТОЛЬКО перед реальным закрытием и снимается сразу после него
				//   (`HandleConnectionClosed` показывает попап синхронно) — иначе 2113 достался бы следующему обычному обрыву.
				UAsdaDisconnectPopupSubsystem* Pop = GI->GetSubsystem<UAsdaDisconnectPopupSubsystem>();
				if (Pop) { Pop->SetNextNotice(AsdaGmOrigUi::kNoticeKicked); }
				Net->CloseAsServerDropped(TEXT("6019 — отключён оператором"));
				if (Pop) { Pop->SetNextNotice(0); }
			}
			break;
		}
		case 6023:   // «Служба Поддержки убивает тебя» — только если [0..3] = свой UserIndex (`netHandleAutoResurrectReady`)
		{
			// ★ Ревизия М-5: прочие шаги эталона (`deathMenuWnd_reset`, состояние 6, опция 4, `percent64`) не переносятся — у сервера
			//   6023 идёт ДО 4053, а порт разбирает его в тике ПОСЛЕ 4053: шаги затёрли бы набор опций меню смерти 4053.
			//   Своё поле — `CUser+0x174` = `SelectedCharId` (ревизия С-2); UserIndex учётки принимается для надёжности.
			if (B.Num() >= 4 && Net && (AsdaBytes::RdI32(B.GetData()) == Net->GetSelfCharUserIndex()
				|| AsdaBytes::RdI32(B.GetData()) == (int32)Net->GetUserIndexForDiagnostics()))
			{
				Say(Loc(AsdaGmOrigUi::kNoticeKilled), AsdaGmOrigUi::Argb(AsdaGmOrigUi::kRed), /*bPlank*/true);
			}
			break;
		}
		case 6025:   // «Служба Поддержки воскрешает тебя» у цели 6024 (`char_onLocalPlayerMapEntry@0xF49090`)
		{
			// ★ [ИСПРАВЛЕНИЕ ДЕФЕКТА] эталон сравнивает dword [0..3] с полным UserIndex, а сервер кладёт userNum(2) + младшее слово
			//   UserIndex(2) — нотис в оригинале почти не показывался (group4_spec §4.4). Сверяем оба поля по их настоящему смыслу.
			if (B.Num() >= 4 && Net && Net->IsSelfUserIdx(AsdaBytes::RdI16(B.GetData()))
				&& (AsdaBytes::RdU16(B.GetData() + 2) == ((uint32)Net->GetSelfCharUserIndex() & 0xFFFFu)
					|| AsdaBytes::RdU16(B.GetData() + 2) == (Net->GetUserIndexForDiagnostics() & 0xFFFFu)))
			{
				ApplyGmRevive6025();
			}
			break;
		}
		case 6058:   // сброс откатов навыков — только если [0..1] = свой userNum (`skillHotbarRefreshForUser`)
			if (B.Num() >= 2 && Net && Net->IsSelfUserIdx(AsdaBytes::RdI16(B.GetData()))) { ApplyCoolReset6058(); }
			break;
		case 6145:   // обратный отсчёт: [0..3] секунды, [4] охват, [5..6] действие
			if (B.Num() >= 7) { StartCountDown(AsdaBytes::RdI32(B.GetData()), (int32)B[4], AsdaBytes::RdI16(B.GetData() + 5)); }
			break;
		case 6147:   // блок карты: читается только [0]
			if (B.Num() >= 1) { ApplyMapBlock6147(B[0] == 1); }
			break;
		case 6027:   // «Служба Поддержки переместила тебя» (тело не читается, `uiShowLocalizedNotice2118`)
			Say(Loc(AsdaGmOrigUi::kNoticeMoved), AsdaGmOrigUi::Argb(AsdaGmOrigUi::kRed), /*bPlank*/true);
			break;
		case 6034:
			DumpMobCard6034(B);
			break;
		case 6037:
			if (bStatsPending) { bStatsPending = false; DumpStats6037(B); }   // проба прав (без запроса) в чат не печатается
			break;
		case 6043:   // итог выдачи предмета оператору — литералы эталона
		{
			const TCHAR* T = (B.Num() >= 1) ? AsdaGmOrigUi::ItemPopText(B[0]) : nullptr;
			if (T)
			{
				AsdaNotice::Plank(GI, EAsdaMsgCategory::System, T, AsdaGmOrigUi::Argb(AsdaGmOrigUi::kGold), (float)AsdaGmOrigUi::kTtl);
				Say(T, AsdaGmOrigUi::Argb(AsdaGmOrigUi::kChatGm));
			}
			break;
		}
		case 6044:   // предмет получателю: [0..1] userNum, [6] склад, [7..63] запись 57 б (`netHandleItemSlotUpdate`)
		{
			if (B.Num() >= 64 && Net && Net->IsSelfUserIdx(AsdaBytes::RdI16(B.GetData())))
			{
				TArray<uint8> Rec;
				Rec.Append(B.GetData() + 7, 57);
				UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>();
				const bool bApplied = Inv && Inv->ApplyExternalRecord(Rec);
				UE_LOG(LogAsdaGmOrigUi, Log, TEXT("[GM-ориг] 6044 предмет от оператора: код=%d контейнер=%u ячейка=%d ×%d склад=%u -> %s"),
					AsdaBytes::RdI32(Rec.GetData()), (uint32)Rec[4], AsdaBytes::RdI16(Rec.GetData() + 5), AsdaBytes::RdI32(Rec.GetData() + 9),
					(uint32)B[6], bApplied ? TEXT("применён") : TEXT("НЕ применён"));
			}
			break;
		}
		case 6075:
			DumpCharValue6075(B);
			break;
		default:
			break;
	}
}

void UAsdaGmOriginalSubsystem::DumpMobCard6034(const TArray<uint8>& B)
{
	// `ItemMixResult_DumpToChat` (etalon/item_inventory.c:67615-67700, group2 §4.4): заголовки −15132161, предметы −58881,
	//   самоцветы −15073281; «предмет или самоцвет» — поиском записи (сначала таблица предметов, затем самоцветов), а не диапазоном.
	if (B.Num() < 395) { UE_LOG(LogAsdaGmOrigUi, Warning, TEXT("[GM-ориг] 6034: тело %d б < 395"), B.Num()); return; }
	const UAsdaItemTableSubsystem* Items = GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>();
	const FColor CHead = AsdaGmOrigUi::Argb(AsdaGmOrigUi::kChatGm), CItem = AsdaGmOrigUi::Argb(-58881), CSow = AsdaGmOrigUi::Argb(-15073281);
	// 0 — нет записи, 1 — предмет, 2 — самоцвет.
	auto Kind = [Items](int32 Code) -> int32
	{
		if (!Items || Code <= 0) { return 0; }
		if (Items->GetItem(Code)) { return 1; }
		FAsdaSowellRow Row;
		return Items->GetSowell(Code, Row) ? 2 : 0;
	};
	auto Name = [Items](int32 Code) { return Items ? Items->GetItemName(Code) : FString(); };
	const uint8* P = B.GetData();
	Say(FString::Printf(TEXT("● Monster Drop Item : %d(%%)"), (int32)(int8)P[82]), CHead);
	Say(TEXT("● Monster Drop Item List:"), CHead);
	for (int32 i = 0; i < 20; ++i)
	{
		const int32 Code = AsdaBytes::RdI32(P + 83 + 10 * i);
		const double Pct = AsdaBytes::RdI32(P + 87 + 10 * i) * 0.001;
		const int32 K = Kind(Code);
		if (K == 1) { Say(FString::Printf(TEXT(" Item - index:%d Name:%s Percent:%f(%%)"), Code, *Name(Code), Pct), CItem); }
		else if (K == 2) { Say(FString::Printf(TEXT(" Sowel - index:%d Name:%s Percent:%f(%%)"), Code, *Name(Code), Pct), CSow); }
	}
	Say(TEXT("● Monster Party Drop Item List:"), CHead);
	for (int32 i = 0; i < 2; ++i)
	{
		const int32 Code = AsdaBytes::RdI32(P + 283 + 10 * i);
		// ★ [ИСПРАВЛЕНИЕ] эталон у предмета брал шанс из первого списка (+87+10i) и печатал `%d` от double; здесь поле +287+10i и `%f`.
		const double Pct = AsdaBytes::RdI32(P + 287 + 10 * i) * 0.001;
		const int32 K = Kind(Code);
		if (K == 1) { Say(FString::Printf(TEXT(" index:%d Name:%s Percent:%f(%%)"), Code, *Name(Code), Pct), CItem); }
		else if (K == 2) { Say(FString::Printf(TEXT(" Sowel - index:%d Name:%s Percent:%f(%%)"), Code, *Name(Code), Pct), CSow); }
	}
	Say(TEXT("● World Drop Item List:"), CHead);
	for (int32 i = 0; i < 2; ++i)
	{
		// ★ [ИСПРАВЛЕНИЕ] эталон в ветке самоцвета искал код по +283+10i (список отряда); здесь тот же код +311+4i.
		const int32 Code = AsdaBytes::RdI32(P + 311 + 4 * i);
		const double Pct = AsdaBytes::RdI32(P + 391 + 4 * i) * 0.001;
		const int32 K = Kind(Code);
		if (K == 1) { Say(FString::Printf(TEXT(" index:%d Name:%s Percent:%f(%%)"), Code, *Name(Code), Pct), CItem); }
		else if (K == 2) { Say(FString::Printf(TEXT(" Sowel - index:%d Name:%s Percent:%f(%%)"), Code, *Name(Code), Pct), CSow); }
	}
}

void UAsdaGmOriginalSubsystem::DumpStats6037(const TArray<uint8>& B)
{
	// `chatDumpCharacterStats@0xF4B260` (etalon/chat.c:6751-6818): порядок, форматы (.rdata 0x1444760…0x1444944) и цвет −15132161
	//   эталона. Отступления помечены. Поля с +108 у оригинала недостоверны: NpcServer отвечает 108 б из 135 (group2 §6.3).
	if (B.Num() < 108) { UE_LOG(LogAsdaGmOrigUi, Warning, TEXT("[GM-ориг] 6037: тело %d б < 108"), B.Num()); return; }
	const uint8* P = B.GetData();
	const FColor C = AsdaGmOrigUi::Argb(AsdaGmOrigUi::kChatGm);
	auto I16 = [P](int32 Off) { return (int32)AsdaBytes::RdI16(P + Off); };
	// ★ [ИСПРАВЛЕНИЕ] эталон собирал строку уровня `printf` вместо `sprintf` и выводил ПУСТУЮ строку.
	Say(FString::Printf(TEXT("● %s : %d"), *Loc(AsdaGmOrigUi::kLocLevel), (int32)P[0]), C);
	Say(FString::Printf(TEXT("● HP : %d/%d"), AsdaBytes::RdI32(P + 37), AsdaBytes::RdI32(P + 31)), C);
	Say(FString::Printf(TEXT("● MP: %d/%d"), I16(41), I16(35)), C);
	// ★ [ИСПРАВЛЕНИЕ] эталон печатал `%d` от 64-битного опыта (младшее слово); здесь всё число.
	const int64 Exp = (int64)AsdaBytes::RdU32(P + 1) | ((int64)AsdaBytes::RdI32(P + 5) << 32);
	Say(FString::Printf(TEXT("● %s : %lld"), *Loc(AsdaGmOrigUi::kLocExp), (long long)Exp), C);
	Say(FString::Printf(TEXT("● Weight: %d"), I16(9)), C);
	Say(FString::Printf(TEXT("● Attack Velocity: %d %d %d %d %d %d"), I16(19), I16(21), I16(23), I16(25), I16(27), I16(29)), C);
	// ★ [ИСПРАВЛЕНИЕ] строку «● Move velocity: %f» эталон кормил i16 веса (+9) под `%f` — печатался мусор; поля скорости
	//   движения в теле нет, поэтому строку не выводим.
	Say(FString::Printf(TEXT("● Attack  : (MIN) %d   (MAX) %d"), I16(43), I16(45)), C);
	Say(FString::Printf(TEXT("● Magic Attack value : (MIN) %d   (MAX) %d"), I16(47), I16(49)), C);
	Say(FString::Printf(TEXT("● Defense : (MIN) %d   (MAX) %d"), I16(53), I16(55)), C);
	Say(FString::Printf(TEXT("● Defense2 : (MIN) %d (%%)   (MAX) %d (%%)"), (int32)P[57], (int32)P[58]), C);
	Say(FString::Printf(TEXT("● Magic Defense: %d"), I16(51)), C);
	Say(FString::Printf(TEXT("● Block : (MIN) %d   (MAX) %d"), I16(61), I16(61)), C);   // эталон: одно поле +61 дважды
	// Характеристики «сумма = (Base) + (Add)»: база +99…+109, добавка +111…+121. Достоверна только база первых четырёх
	//   (+99…+106); добавки и база Интеллекта/Удачи лежат за 108-м байтом — печатаем базу, остальное — строкой-пометкой.
	for (int32 i = 0; i < 4; ++i)
	{
		Say(FString::Printf(TEXT("●  %s  : (Base) %d"), *Loc(AsdaGmOrigUi::kLocStat0 + i), I16(99 + 2 * i)), C);
	}
	Say(Loc(AsdaGmOrigUi::kStatsUnreliable), C);
	Say(FString::Printf(TEXT("●  %s!   : %d "), *Loc(AsdaGmOrigUi::kLocGold), AsdaBytes::RdI32(P + 11)), C);
}

void UAsdaGmOriginalSubsystem::DumpCharValue6075(const TArray<uint8>& B)
{
	// `Debug_DumpTargetCombatStats@0xA124E0` (group3 §4.3): разделитель + 11 строк. Подписи «Target…» у эталона вводят в
	//   заблуждение (атака/магия/уклонение/крит — самого оператора; проценты — оператора против уровня цели): печатаем как эталон.
	if (B.Num() < 24) { UE_LOG(LogAsdaGmOrigUi, Warning, TEXT("[GM-ориг] 6075: тело %d б < 24"), B.Num()); return; }
	const uint8* P = B.GetData();
	const FColor C = AsdaGmOrigUi::Argb(AsdaGmOrigUi::kBlue);
	Say(TEXT("-------------------"), C);
	Say(FString::Printf(TEXT("TargetType = %d"), (int32)P[6]), C);
	Say(FString::Printf(TEXT("TargetIndex = %d"), (int32)AsdaBytes::RdI16(P + 7)), C);
	Say(FString::Printf(TEXT("TargetMinAttack = %d"), (int32)AsdaBytes::RdI16(P + 9)), C);
	Say(FString::Printf(TEXT("TargetMaxAttack = %d"), (int32)AsdaBytes::RdI16(P + 11)), C);
	Say(FString::Printf(TEXT("TargetMinMagic = %d"), (int32)AsdaBytes::RdI16(P + 13)), C);
	Say(FString::Printf(TEXT("TargetMaxMagic = %d"), (int32)AsdaBytes::RdI16(P + 15)), C);
	Say(FString::Printf(TEXT("TargetEvasion = %d"), (int32)AsdaBytes::RdI16(P + 17)), C);
	Say(FString::Printf(TEXT("TargetEvasionRate = %d"), (int32)P[19]), C);
	Say(FString::Printf(TEXT("TargetCritical = %d"), (int32)AsdaBytes::RdI16(P + 20)), C);
	Say(FString::Printf(TEXT("TargetCriticalRate = %d"), (int32)P[22]), C);
	Say(FString::Printf(TEXT("TargetDefense = %d"), (int32)P[23]), C);
}

// ═════════ Разбор ввода ═══════════════════════════════════════════════════════════════════════════════════

bool UAsdaGmOriginalSubsystem::ParseCell(const FString& S, int32& OutX, int32& OutY)
{
	const TArray<FString> W = AsdaGmOrigUi::Words(S);
	return W.Num() == 2 && AsdaGmOrigUi::Int(W[0], OutX) && AsdaGmOrigUi::Int(W[1], OutY) && OutX > 0 && OutY > 0 && OutX < 32768 && OutY < 32768;
}

bool UAsdaGmOriginalSubsystem::ParseItemGive(const FString& S, int32& OutCode, int32& OutAmount, bool& bOutWarehouse)
{
	const TArray<FString> W = AsdaGmOrigUi::Words(S);
	if (W.Num() < 1 || W.Num() > 3 || !AsdaGmOrigUi::Int(W[0], OutCode) || OutCode <= 0) { return false; }
	OutAmount = 1;
	if (W.Num() >= 2 && (!AsdaGmOrigUi::Int(W[1], OutAmount) || OutAmount <= 0)) { return false; }
	bOutWarehouse = false;
	if (W.Num() == 3)
	{
		const FString K = W[2].ToLower();
		if (K == TEXT("склад") || K == TEXT("warehouse") || K == TEXT("1")) { bOutWarehouse = true; }
		else if (K != TEXT("сумка") && K != TEXT("bag") && K != TEXT("0")) { return false; }
	}
	return true;
}

bool UAsdaGmOriginalSubsystem::ParseGroup(const FString& S, int32& OutGroup, int32& OutMode)
{
	const TArray<FString> W = AsdaGmOrigUi::Words(S);
	if (W.Num() < 1 || W.Num() > 2 || !AsdaGmOrigUi::Int(W[0], OutGroup) || OutGroup < 0 || OutGroup > 65535) { return false; }
	OutMode = 0;
	return W.Num() == 1 || (AsdaGmOrigUi::Int(W[1], OutMode) && OutMode >= 0 && OutMode <= 255);
}

bool UAsdaGmOriginalSubsystem::ParseEventSet(const FString& S, FEventSetArgs& Out)
{
	// Порядок: тип множитель канал карта ДД.ММ ЧЧ:ММ ДД.ММ ЧЧ:ММ мин макс (10 слов).
	const TArray<FString> W = AsdaGmOrigUi::Words(S);
	if (W.Num() != 10) { return false; }
	int32 T = 0, Lo = 0, Hi = 0;
	if (!AsdaGmOrigUi::Int(W[0], T) || T < 0 || T > 1) { return false; }
	float M = 0.f;
	if (!AsdaGmOrigUi::Float(W[1], M) || M < 0.f || M > 10.f) { return false; }   // сервер: 0.0…10.0
	if (!AsdaGmOrigUi::Int(W[2], Out.Channel) || !AsdaGmOrigUi::Int(W[3], Out.Map) || Out.Channel < -1 || Out.Map < -1) { return false; }
	if (!AsdaGmOrigUi::DayMon(W[4], Out.SDay, Out.SMon) || !AsdaGmOrigUi::HourMin(W[5], Out.SHour, Out.SMin)) { return false; }
	if (!AsdaGmOrigUi::DayMon(W[6], Out.EDay, Out.EMon) || !AsdaGmOrigUi::HourMin(W[7], Out.EHour, Out.EMin)) { return false; }
	if (!AsdaGmOrigUi::Int(W[8], Lo) || !AsdaGmOrigUi::Int(W[9], Hi) || Lo < 0 || Hi > 255 || Lo > Hi) { return false; }
	Out.Type = (uint8)T; Out.Mult = M; Out.MinLevel = (uint8)Lo; Out.MaxLevel = (uint8)Hi;
	return true;
}

bool UAsdaGmOriginalSubsystem::ParseEventOff(const FString& S, uint8& OutType, int32& OutChannel, int32& OutMap)
{
	const TArray<FString> W = AsdaGmOrigUi::Words(S);
	int32 T = 0;
	if (W.Num() != 3 || !AsdaGmOrigUi::Int(W[0], T) || T < 0 || T > 1 || !AsdaGmOrigUi::Int(W[1], OutChannel) || !AsdaGmOrigUi::Int(W[2], OutMap)) { return false; }
	OutType = (uint8)T;
	return OutChannel >= -1 && OutMap >= -1;
}

FString UAsdaGmOriginalSubsystem::DefaultEventSetText()
{
	// NpcServer сравнивает окно ивента по локальному времени сервера (group3 §1.4); берём локальное время клиента как подсказку.
	const FDateTime Now = FDateTime::Now();
	const FDateTime End = Now + FTimespan::FromHours(24.0);
	return FString::Printf(TEXT("1 2.0 -1 -1 %02d.%02d %02d:%02d %02d.%02d %02d:%02d 1 100"),
		Now.GetDay(), Now.GetMonth(), Now.GetHour(), Now.GetMinute(), End.GetDay(), End.GetMonth(), End.GetHour(), End.GetMinute());
}

// ── Группа 4 (`group4_spec.md` §11) ─────────────────────────────────────────────────────────────────────────

bool UAsdaGmOriginalSubsystem::GetSelfCell(APlayerController* PC, int32& OutX, int32& OutZ) const
{
	const APawn* P = PC ? PC->GetPawn() : nullptr;
	if (!P) { OutX = OutZ = 0; return false; }
	const FIntPoint T = AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(P->GetActorLocation()));
	OutX = T.X; OutZ = T.Y;
	return true;
}

void UAsdaGmOriginalSubsystem::ActCallMap()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
	if (!WM) { return; }
	// По умолчанию — текущая карта (индекс MapInfo.ini = номер Load Manager'а, group4 §1.1).
	const UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
	const FString Initial = (LM && LM->CurrentMapId >= 0) ? FString::FromInt(LM->CurrentMapId) : FString();
	TWeakObjectPtr<UAsdaGmOriginalSubsystem> Self(this);
	WM->ShowInputDialog(Loc(AsdaGmOrigUi::kMapTitle), Loc(AsdaGmOrigUi::kMapGuide), Initial, 10, [Self](bool bOk, const FString& Text)
	{
		UAsdaGmOriginalSubsystem* S = Self.Get();
		UAsdaWindowSubsystem* WM2 = S ? S->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!bOk || !S || !WM2) { return; }
		int32 Map = -1;
		const TArray<FString> W = AsdaGmOrigUi::Words(Text);
		if (W.Num() != 1 || !AsdaGmOrigUi::Int(W[0], Map) || Map < 0 || Map > 32767)
		{
			S->Say(FString::Printf(TEXT("%s: %s"), *S->Loc(AsdaGmOrigUi::kBadInput), *Text), FColor(0xE9, 0xD6, 0xBC));
			return;
		}
		// Подтверждение: каждый игрок карты M своего канала сразу уходит в переход к оператору (6013), group4 §1.4.
		AsdaEnterGuard::NoEnter(WM2)->ShowGameConfirm(S->Loc(AsdaGmOrigUi::kConfirmCallMap).Replace(TEXT("[ss1]"), *FString::FromInt(Map)), [Self, Map](bool bYes)
		{
			UAsdaGmOriginalSubsystem* S2 = Self.Get();
			UAsdaNetSubsystem* Net = S2 ? S2->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			if (bYes && S2 && Net) { S2->SaySent(Net->SendGmCallMap4209(Map), FString::Printf(TEXT("%s %d"), *S2->Loc(990800), Map)); }
		});
	});
}

void UAsdaGmOriginalSubsystem::ActClearItems()
{
	UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
	if (!WM) { return; }
	TWeakObjectPtr<UAsdaGmOriginalSubsystem> Self(this);
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Loc(AsdaGmOrigUi::kConfirmClearItems), [Self](bool bYes)
	{
		UAsdaGmOriginalSubsystem* S = Self.Get();
		UAsdaNetSubsystem* Net = S ? S->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		// Ответа оператору нет: предметы просто исчезают (5015 сектору), group4 §2.5.
		if (bYes && S && Net) { S->SaySent(Net->SendGmClearGroundItems6007(), S->Loc(990802)); }
	});
}

void UAsdaGmOriginalSubsystem::ActJump(APlayerController* PC)
{
	UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
	if (!WM) { return; }
	int32 X = 0, Z = 0;
	const FString Initial = GetSelfCell(PC, X, Z) ? FString::Printf(TEXT("%d %d"), X, Z) : FString();
	TWeakObjectPtr<UAsdaGmOriginalSubsystem> Self(this);
	WM->ShowInputDialog(Loc(990804), Loc(AsdaGmOrigUi::kCellGuide), Initial, 20, [Self](bool bOk, const FString& Text)
	{
		UAsdaGmOriginalSubsystem* S = Self.Get();
		UAsdaNetSubsystem* Net = S ? S->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!bOk || !S || !Net) { return; }
		int32 CX = 0, CZ = 0;
		if (!ParseCell(Text, CX, CZ)) { S->Say(FString::Printf(TEXT("%s: %s"), *S->Loc(AsdaGmOrigUi::kBadInput), *Text), FColor(0xE9, 0xD6, 0xBC)); return; }
		// Видимый результат — 4056 себе (штатный приёмник); клетка вне карты или непроходима — тишина сервера.
		S->SaySent(Net->SendGmJump6010(CX, CZ), FString::Printf(TEXT("%s → %d, %d"), *S->Loc(990804), CX, CZ));
	});
}

// ═════════ ★ 09.11.2026 GM-телепорт Ctrl+ЛКМ по мини-карте / карте мира ═══════════════════════════════════════════

EAsdaGmMapTp UAsdaGmOriginalSubsystem::TryMapClickTeleport(int32 ClickMapId, int32 SelfMapId, int32 TileX, int32 TileZ,
	int32 MapW, int32 MapH, bool bBigMap)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	// Обычный игрок: не перехватываем и НИЧЕГО не шлём — ни пробы, ни 6010; клик идёт своим обычным путём.
	if (CVarAsdaGmMapClickTeleport.GetValueOnGameThread() == 0 || !Net || !AsdaNpcGmOnly::SelfIsGm(GI)) { return EAsdaGmMapTp::NotGm; }
	const TCHAR* Src = bBigMap ? TEXT("карта мира") : TEXT("мини-карта");

	// Чужая карта (только карта мира с другим регионом): форма P123 6010 (тело 8 б: X, Z, карта, метка) — сервер P123 проверяет карту
	//   (тип, OpenMapInfo, Total_PortalConfig, поток), клетку по сетке карты назначения, сдвигает на проходимую и шлёт 5055 код 1
	//   (обычный переход между картами). Автопуть по такому клику не взводим: Ctrl+клик GM — телепорт.
	//   ★ Ключ `asda.Gm.MapTeleportOtherMap 0` — прежнее поведение (на чужую карту ничего не шлётся, строка 991300).
	const bool bOther = ClickMapId != SelfMapId;
	if (bOther && CVarAsdaGmMapTeleportOtherMap.GetValueOnGameThread() == 0)
	{
		Say(Loc(AsdaGmMapTp::kOtherMap), FColor(0xE9, 0xD6, 0xBC));
		UE_LOG(LogAsdaGmOrigUi, Log, TEXT("[GM-телепорт] %s: карта %d ≠ своя %d, клетка (%d,%d) — ключ OtherMap 0, не отправлено"),
			Src, ClickMapId, SelfMapId, TileX, TileZ);
		return EAsdaGmMapTp::OtherMap;
	}
	// Границы — та же проверка, что у сервера (`IsCheckPosInMaxPos`: 0 < X < w, 0 < Z < h); у чужой карты размер проверяет сервер P123.
	if (TileX <= 0 || TileZ <= 0 || (!bOther && MapW > 0 && TileX >= MapW) || (!bOther && MapH > 0 && TileZ >= MapH) || TileX > 32767 || TileZ > 32767)
	{
		Say(Loc(AsdaGmMapTp::kOutOfMap).Replace(TEXT("[ss1]"), *FString::FromInt(TileX)).Replace(TEXT("[ss2]"), *FString::FromInt(TileZ)),
			FColor(0xE9, 0xD6, 0xBC));
		return EAsdaGmMapTp::OutOfMap;
	}
	if (Net->IsOperatorConfirmedByServer())
	{
		bTpPending = false;
		SendMapTpJump(TileX, TileZ, bOther ? ClickMapId : -1);
		return bOther ? EAsdaGmMapTp::SentOtherMap : EAsdaGmMapTp::Sent;
	}
	// Права в этой сессии ещё не подтверждались: одна проба 6036 (сервер только читает и отвечает 6037), клик — по ответу.
	if (bTpPending)
	{
		TpX = TileX; TpZ = TileZ; TpDest = bOther ? ClickMapId : -1;   // новая клетка вместо прежней, проба уже в пути
		return EAsdaGmMapTp::ProbePending;
	}
	if (!Net->CanSendGmRightsProbe())
	{
		Say(Loc(AsdaGmMapTp::kNoRights), FColor(0xE9, 0xD6, 0xBC));
		return EAsdaGmMapTp::NoRights;
	}
	TpProbeSeq = Net->GetGmRightsAnswerSeq();
	if (!Net->SendGmRightsProbe6036())
	{
		Say(Loc(AsdaGmMapTp::kNoRights), FColor(0xE9, 0xD6, 0xBC));
		return EAsdaGmMapTp::NoRights;
	}
	bTpPending = true;
	TpMapId = SelfMapId; TpX = TileX; TpZ = TileZ; TpDest = bOther ? ClickMapId : -1;
	TpDeadline = FPlatformTime::Seconds() + AsdaGmMapTp::kProbeWaitSec;
	UE_LOG(LogAsdaGmOrigUi, Log, TEXT("[GM-телепорт] %s: клетка (%d,%d) — права не подтверждены, проба 6036, ждём 6037"), Src, TileX, TileZ);
	return EAsdaGmMapTp::ProbePending;
}

void UAsdaGmOriginalSubsystem::TickMapTpPending()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	if (!Net || (LM && LM->CurrentMapId != TpMapId)) { bTpPending = false; return; }   // карта сменилась — клик устарел
	if (Net->IsOperatorConfirmedByServer())
	{
		bTpPending = false;
		SendMapTpJump(TpX, TpZ, TpDest);
		return;
	}
	if (Net->GetGmRightsAnswerSeq() != TpProbeSeq || FPlatformTime::Seconds() > TpDeadline)
	{
		bTpPending = false;
		Say(Loc(AsdaGmMapTp::kNoRights), FColor(0xE9, 0xD6, 0xBC));
		UE_LOG(LogAsdaGmOrigUi, Log, TEXT("[GM-телепорт] права сервером не подтверждены — клетка (%d,%d) не отправлена"), TpX, TpZ);
	}
}

void UAsdaGmOriginalSubsystem::SendMapTpJump(int32 X, int32 Z, int32 DestMap)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	if (DestMap < 0)
	{
		// Своя карта: видимый результат — 4056 себе (штатный приёмник). Непроходимую клетку сервер сам сдвигает к ближайшей проходимой.
		SaySent(Net->SendGmJump6010(X, Z), FString::Printf(TEXT("%s → %d, %d"), *Loc(AsdaGmMapTp::kJumpName), X, Z));
		return;
	}
	// Другая карта (P123): видимый результат — 5055 код 1 → переход на карту. Отказ сервера молчаливый (как у стоковой 6010).
	SaySent(Net->SendGmJump6010(X, Z, DestMap), Loc(AsdaGmMapTp::kJumpMap).Replace(TEXT("[ss1]"), *FString::FromInt(DestMap))
		.Replace(TEXT("[ss2]"), *FString::FromInt(X)).Replace(TEXT("[ss3]"), *FString::FromInt(Z)));
}

void UAsdaGmOriginalSubsystem::ActRevive(APlayerController* PC)
{
	int32 Uid = -1, UIdx = -1; FString Name;
	if (!GetTargetPlayer(PC, Uid, UIdx, Name, /*bAllowSelf*/true)) { return; }
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	if (!Net) { return; }
	// Подтверждения у оригинала нет (group4 §4.5): мёртвый встаёт на месте, живой получает полные HP/MP.
	SaySent(Net->SendGmRevive6024(Uid, UIdx), Loc(990806) + TEXT(" «") + Name + TEXT("»"));
}

void UAsdaGmOriginalSubsystem::ActCoolReset(APlayerController* PC)
{
	int32 Uid = -1, UIdx = -1; FString Name;
	if (!GetTargetPlayer(PC, Uid, UIdx, Name, /*bAllowSelf*/true)) { return; }
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	if (!Net) { return; }
	// Ответ 6058 уходит ЦЕЛИ (не оператору, если цель — другой игрок), group4 §5.3.
	SaySent(Net->SendGmCoolReset6058(Uid), Loc(990808) + TEXT(" «") + Name + TEXT("»"));
}

void UAsdaGmOriginalSubsystem::ActCountDown()
{
	UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
	if (!WM) { return; }
	TWeakObjectPtr<UAsdaGmOriginalSubsystem> Self(this);
	WM->ShowInputDialog(Loc(AsdaGmOrigUi::kCountTitle), Loc(AsdaGmOrigUi::kCountGuide), TEXT("10 0 0"), 20, [Self](bool bOk, const FString& Text)
	{
		UAsdaGmOriginalSubsystem* S = Self.Get();
		UAsdaNetSubsystem* Net = S ? S->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!bOk || !S || !Net) { return; }
		int32 Sec = 0, Scope = 0, Action = 0;
		if (!ParseCountDown(Text, Sec, Scope, Action)) { S->Say(FString::Printf(TEXT("%s: %s"), *S->Loc(AsdaGmOrigUi::kBadInput), *Text), FColor(0xE9, 0xD6, 0xBC)); return; }
		// Ответ 6145 приходит и самому оператору (он в охвате) — отсчёт показывает штатный приёмник; здесь — только отказ.
		if (!Net->SendGmCountDown6145(Sec, (uint8)Scope, Action)) { S->SaySent(false, S->Loc(990810)); }
	});
}

void UAsdaGmOriginalSubsystem::ActAreaJump(APlayerController* PC)
{
	UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
	if (!WM) { return; }
	// Шаблон: квадрат 10×10 клеток вокруг себя → в свою клетку.
	int32 X = 0, Z = 0;
	const FString Initial = GetSelfCell(PC, X, Z)
		? FString::Printf(TEXT("%d %d %d %d %d %d"), FMath::Max(1, X - 5), FMath::Max(1, Z - 5), X + 5, Z + 5, X, Z) : FString();
	TWeakObjectPtr<UAsdaGmOriginalSubsystem> Self(this);
	WM->ShowInputDialog(Loc(AsdaGmOrigUi::kAreaTitle), Loc(AsdaGmOrigUi::kAreaGuide), Initial, 40, [Self](bool bOk, const FString& Text)
	{
		UAsdaGmOriginalSubsystem* S = Self.Get();
		UAsdaWindowSubsystem* WM2 = S ? S->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!bOk || !S || !WM2) { return; }
		TArray<int32> Six;
		Six.SetNumZeroed(6);
		int32 Arr[6] = {};
		if (!ParseAreaJump(Text, Arr)) { S->Say(FString::Printf(TEXT("%s: %s"), *S->Loc(AsdaGmOrigUi::kBadInput), *Text), FColor(0xE9, 0xD6, 0xBC)); return; }
		for (int32 i = 0; i < 6; ++i) { Six[i] = Arr[i]; }
		const FString Desc = FString::Printf(TEXT("(%d,%d)…(%d,%d) → %d, %d"), Arr[0], Arr[1], Arr[2], Arr[3], Arr[4], Arr[5]);
		AsdaEnterGuard::NoEnter(WM2)->ShowGameConfirm(S->Loc(AsdaGmOrigUi::kConfirmArea).Replace(TEXT("[ss1]"), *Desc), [Self, Six, Desc](bool bYes)
		{
			UAsdaGmOriginalSubsystem* S2 = Self.Get();
			UAsdaNetSubsystem* Net = S2 ? S2->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			if (!bYes || !S2 || !Net) { return; }
			int32 A[6];
			for (int32 i = 0; i < 6; ++i) { A[i] = Six[i]; }
			// Режим 1, userNum −1 (как `recallCountdown_Tick`); операторы не переносятся (group4 §7.3).
			S2->SaySent(Net->SendClientTargetJump6146(1, -1, A), S2->Loc(990812) + TEXT(" ") + Desc);
		});
	});
}

void UAsdaGmOriginalSubsystem::ActMapBlock(bool bOn)
{
	UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
	if (!WM) { return; }
	TWeakObjectPtr<UAsdaGmOriginalSubsystem> Self(this);
	// Зоны зашиты в клиенте координатами поля боя quizstage; номер карты оригинал не проверяет (group4_client_handlers §4.2).
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Loc(bOn ? AsdaGmOrigUi::kConfirmBlockOn : AsdaGmOrigUi::kConfirmBlockOff), [Self, bOn](bool bYes)
	{
		UAsdaGmOriginalSubsystem* S = Self.Get();
		UAsdaNetSubsystem* Net = S ? S->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		// Тело меню — инициализатор оригинала `sub_55F980`: [1..4] = [5..8] = −1 (их никто не читает). Ответ 6147 — всем на
		//   карте, включая оператора: нотис 108141/108142 печатает штатный приёмник.
		if (bYes && S && Net && !Net->SendGmMapBlock6147(bOn ? 1 : 0, -1, -1)) { S->SaySent(false, S->Loc(bOn ? 990814 : 990816)); }
	});
}

bool UAsdaGmOriginalSubsystem::ParseCountDown(const FString& S, int32& OutSec, int32& OutScope, int32& OutAction)
{
	const TArray<FString> W = AsdaGmOrigUi::Words(S);
	OutScope = 0; OutAction = 0;
	if (W.Num() < 1 || W.Num() > 3 || !AsdaGmOrigUi::Int(W[0], OutSec) || OutSec <= 0 || OutSec > 86400) { return false; }
	if (W.Num() >= 2 && (!AsdaGmOrigUi::Int(W[1], OutScope) || OutScope < 0 || OutScope > 2)) { return false; }
	if (W.Num() == 3 && (!AsdaGmOrigUi::Int(W[2], OutAction) || OutAction < 0 || OutAction > 2)) { return false; }
	return true;
}

bool UAsdaGmOriginalSubsystem::ParseAreaJump(const FString& S, int32 (&Out)[6])
{
	const TArray<FString> W = AsdaGmOrigUi::Words(S);
	if (W.Num() != 6) { return false; }
	for (int32 i = 0; i < 6; ++i)
	{
		if (!AsdaGmOrigUi::Int(W[i], Out[i]) || Out[i] <= 0 || Out[i] > 32767) { return false; }
	}
	return Out[0] <= Out[2] && Out[1] <= Out[3];
}

// ═════════ Группа 4: приёмники у цели / охвата (`group4_client_handlers.md`) ═════════════════════════════════

void UAsdaGmOriginalSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Ответы старой карты на новой не разбираем (ревизия М-13: стоп 6147 по координатам новой карты был бы ложным).
	Queue.Reset();
	// Клик GM-телепорта относится к карте, на которой щёлкнули: после смены карты/разрыва он не исполняется.
	bTpPending = false;
	// Эталон: `recallCountdown_Init` и конструктор зон — только при входе в мир; смена карты отсчёт и блок не трогает.
	if (!AsdaIsFullSessionReset(Reason)) { return; }
	bCdRunning = false; CdDurationMs = -1.f; CdElapsedMs = 0.f; CdAction = 1; CdNoticeAccumMs = 0.f;
	bMapBlock = false; MapBlockZone = -1;
	bStatsPending = false;
	Queue.Reset();
}

AAsdaPlayerCharacter* UAsdaGmOriginalSubsystem::SelfChar() const
{
	const UGameInstance* GI = GetGameInstance();
	const APlayerController* PC = GI ? GI->GetFirstLocalPlayerController(GI->GetWorld()) : nullptr;
	return PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
}

void UAsdaGmOriginalSubsystem::ApplyGmRevive6025()
{
	// `char_onLocalPlayerMapEntry@0xF49090` (entity_char.c:53188-53257), разбор §1.2. У порта 6025 разбирается в тике — уже
	//   после 4056 того же кадра, который снял состояние смерти (`OnCharRevived`); здесь — остальные шаги эталона.
	UGameInstance* GI = GetGameInstance();
	AAsdaPlayerCharacter* Ch = SelfChar();
	// п.1 `uiCloseOnExchangeItemType`: модалки смерти 2370/2371/2376 снять.
	if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { WM->CloseDeathMsgBoxOnMapEntry(true); }
	// п.2 звук входа `S_c_entry.wav` (эффекта `cmchar01` у 6025 нет).
	if (Ch)
	{
		if (UAsdaSoundSubsystem* Snd = GI->GetSubsystem<UAsdaSoundSubsystem>()) { Snd->PlaySound3D(FName(kAsdaEntrySound), Ch->GetActorLocation()); }
		// п.3–4 состояние 0 и поза покоя: окраска смерти снимается, мёртвая поза → покой (те же шаги, что у
		//   `UAsdaWorldSubsystem::OnPlayerCharRevived`; живого и идущего не останавливаем — 6025 стопа серверу не шлёт).
		Ch->SetDeathDesaturation(false);
		if (Ch->AnimState == EAsdaPlayerState::Dead) { Ch->SetAnimState(EAsdaPlayerState::Idle); }
		// п.5 появление с прозрачности, 1500 мс — тот же вызов, что у эффекта входа 4056.
		UAsdaFadeComponent::Begin(Ch, true, AAsdaMobActor::FadeInSeconds);
	}
	// ★ 31.10.2026 [ОРИГИНАЛ] экранное затемнение режим 0 (`setScaleOne` + `set_o9(…,0)`, entity_char.c:53205-53206): чёрный
	//   экран рассеивается за 1500 мс (`asda.Load.FadeSec`), см. шапку затемнения в AsdaLoadManagerSubsystem.cpp.
	if (UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>()) { LM->BeginBlackFadeOut(TEXT("своё 6025")); }
	// п.11 межкартовый автопуть.
	if (UAsdaMinimapSubsystem* Mm = GI->GetSubsystem<UAsdaMinimapSubsystem>()) { Mm->CancelTravelRequest(); }
	// п.12/14 магазин бустеров 15300 целиком («Вид» 15600 и панель покупки), п.13 элитный магазин 6000, п.15 парикмахерская 15000.
	if (UAsdaBoosterPanelSubsystem* Prev = GI->GetSubsystem<UAsdaBoosterPanelSubsystem>())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>(); WM && WM->IsWindowVisible(FName(TEXT("BOOSTER_PREV_WIN")))) { Prev->ClosePrevWindow(); }
	}
	if (UAsdaBoosterShopSubsystem* Bs = GI->GetSubsystem<UAsdaBoosterShopSubsystem>(); Bs && Bs->IsOpen()) { Bs->CloseWindow(); }
	if (UAsdaPremiumShopSubsystem* Ps = GI->GetSubsystem<UAsdaPremiumShopSubsystem>(); Ps && Ps->IsOpen()) { Ps->CloseWindow(); }
	if (UAsdaHairShopSubsystem* Hs = GI->GetSubsystem<UAsdaHairShopSubsystem>(); Hs && Hs->IsSessionOpen()) { Hs->CloseWindow(); }
	// п.18/20 цель снять.
	if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GI->GetFirstLocalPlayerController(GI->GetWorld()))) { PC->ClearTarget(); }
	// п.23 разрешить кнопки трея главного меню (при смерти их запрещают).
	if (UAsdaSystemMenuSubsystem* Menu = GI->GetSubsystem<UAsdaSystemMenuSubsystem>()) { Menu->SetAllButtonsEnabled(true); }
	// п.24 нотис 2117: плашка, затем строка чата, красный, 10 с.
	Say(Loc(2117), AsdaGmOrigUi::Argb(AsdaGmOrigUi::kRed), /*bPlank*/true);
}

void UAsdaGmOriginalSubsystem::ApplyCoolReset6058()
{
	// `skillHotbarRefreshForUser` (skill_buff.c:9-23): реестр откатов окна навыков `CSkillWnd+56` очищается ЦЕЛИКОМ — и обычные
	//   навыки, и навыки Дружбы (word0 = 1); откаты предметов (`CHotbar+56`) не трогаются. Затем дерево окна и страница панели.
	//   ⚠ Альфа невидимости `gap_A42[22676]` (баффы 112/186) у порта не заведена — вернуть 1.0 нечего (отчёт, «Не сделано»).
	UGameInstance* GI = GetGameInstance();
	if (UAsdaSkillWindowSubsystem* Sw = GI->GetSubsystem<UAsdaSkillWindowSubsystem>()) { Sw->ResetAllSkillCooldowns(); }
	if (UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>()) { Hb->ClearSkillCooldowns(); }
	if (UAsdaSoulmateSkillSubsystem* Sm = GI->GetSubsystem<UAsdaSoulmateSkillSubsystem>()) { Sm->ClearAllCooldowns(); }
	UE_LOG(LogAsdaGmOrigUi, Log, TEXT("[GM-ориг] 6058: откаты навыков сброшены оператором"));
}

void UAsdaGmOriginalSubsystem::StartCountDown(int32 Seconds, int32 Scope, int32 Action)
{
	// `netHandleCountDownStart@0xBB6050` (network.c:11436-11455): английский литерал плашкой 221 (золотой, 10 с, в чат НЕ идёт),
	//   затем `sub_ECCCB0`: длительность = сек·1000, «идёт» = 1; `set_o1`: действие.
	const FString Lit = FString::Printf(TEXT("Count Down Start Count: %d Type: %d "), Seconds, Scope);
	AsdaNotice::Plank(GetGameInstance(), EAsdaMsgCategory::System, Lit, AsdaNoticeColor::Gold, (float)AsdaGmOrigUi::kTtl);
	AddLine(Lit);
	CdDurationMs = (float)Seconds * 1000.f;
	CdAction = Action;
	// ★ Ревизия М-11: сервер срок не проверяет; при ≤ 0 эталон ставил «идёт» без хода — признак не взводим.
	bCdRunning = Seconds > 0;
	// ★ [ИСПРАВЛЕНИЕ] эталон не обнуляет прошедшее время и накопитель плашки: повторный 6145 шёл от прежнего старта, а при
	//   новом сроке меньше прошедшего таймер замирал навсегда (отчёт §3.3). Каждый 6145 — отсчёт с нуля.
	CdElapsedMs = 0.f;
	CdNoticeAccumMs = 0.f;
	UE_LOG(LogAsdaGmOrigUi, Log, TEXT("[GM-ориг] 6145: отсчёт %d с, охват %d, действие %d"), Seconds, Scope, Action);
}

void UAsdaGmOriginalSubsystem::TickCountDown(float DeltaMs)
{
	// `recallCountdown_Tick@0xECC9F0` (ui_core.c:86951-86993), 1:1 по арифметике: плашка «Осталось '%d' сек.» и цифра по центру —
	//   когда накопитель СТРОГО больше 1000 мс; значение = ⌊(срок − прошло)/1000⌋; на 0 — стоп и действие (плашки нет).
	if (!bCdRunning || CdElapsedMs < 0.f || CdElapsedMs >= CdDurationMs) { return; }
	CdNoticeAccumMs += DeltaMs;
	CdElapsedMs += DeltaMs;
	const int32 Left = (int32)(int64)(CdDurationMs - CdElapsedMs) / 1000;
	if (Left > 0)
	{
		if (CdNoticeAccumMs > 1000.f)
		{
			const FString T = AsdaNotice::Loc(GetGameInstance(), 108143).Replace(TEXT("%d"), *FString::FromInt(Left));
			AsdaNotice::Plank(GetGameInstance(), EAsdaMsgCategory::System, T, AsdaNoticeColor::Pink, (float)AsdaGmOrigUi::kTtl);
			AddLine(T);
			CdNoticeAccumMs = 0.f;
			SpawnCountDownDigit(Left);
		}
		return;
	}
	CdElapsedMs = 0.f;
	bCdRunning = false;
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	UE_LOG(LogAsdaGmOrigUi, Log, TEXT("[GM-ориг] 6145: отсчёт окончен, действие %d"), CdAction);
	if (!Net) { return; }
	if (CdAction == 1)
	{
		Net->SendGmMapBlock6147(1, 0, 0);   // `netSendPacket6147(g_pChatWnd, 1, 0, 0)`; сервер пропустит только от оператора (P47)
	}
	else if (CdAction == 2)
	{
		const int32 Six[6] = { 240, 214, 270, 228, 195, 189 };   // зона 2 → (195,189), `netSendReq6146(…, 1, −1, …)`
		Net->SendClientTargetJump6146(1, -1, Six);
	}
}

void UAsdaGmOriginalSubsystem::SpawnCountDownDigit(int32 Value)
{
	// `FloatingText_Spawn(g_pFloatingText, (float)(int)((ширина >> 1) − 5), 200.0f, 0x16, "%d")` (ui_core.c:86975-86980): экранные
	//   пиксели без проекции. Геометрия узла (`style_0x16.md`): глиф 2·s = 60 px, цифра i — [X−45+60i, X+15+60i] × [155, 215],
	//   то есть точка (X, Y) лежит на доле 0.75/n ширины и 0.75 высоты строки — тот же якорь, что у надписей порта.
	UGameInstance* GI = GetGameInstance();
	UAsdaFloatingTextSubsystem* FT = GI ? GI->GetSubsystem<UAsdaFloatingTextSubsystem>() : nullptr;
	UGameViewportClient* VP = GI ? GI->GetGameViewportClient() : nullptr;
	if (!FT || !VP) { return; }
	FVector2D Size = FVector2D::ZeroVector;
	VP->GetViewportSize(Size);
	const FString Text = FString::FromInt(Value);
	const float X = (float)(((int32)Size.X >> 1) - 5);
	FT->SpawnScreen(0x16, Text, FVector2D(X, 200.f), FVector2D(0.75f / (float)FMath::Max(1, Text.Len()), 0.75f));
}

int32 UAsdaGmOriginalSubsystem::MapBlockZoneAt(const FVector2D& P)
{
	// `extraMenuInitSlotRects@0xECD010` (ui_core.c:87052-87060): (x1, z1, x2, z2) в мировых единицах; `uiGrid_HitTestCell`
	//   (ui_core.c:87031-87041) — строгие неравенства, первая подходящая. Номер карты (`ctx+8 = 22`) эталон НЕ проверяет.
	static const float kZones[3][4] = { { 22500.f, 22200.f, 25100.f, 23400.f }, { 25900.f, 22200.f, 28500.f, 23400.f },
		{ 24000.f, 21400.f, 27000.f, 22800.f } };
	for (int32 i = 0; i < 3; ++i)
	{
		if (kZones[i][0] < P.X && P.X < kZones[i][2] && kZones[i][1] < P.Y && P.Y < kZones[i][3]) { return i; }
	}
	return -1;
}

void UAsdaGmOriginalSubsystem::ApplyMapBlock6147(bool bOn)
{
	// `netHandleTeleportToPosResponse@0xBB6160` (network.c:11466-11545). ⚠ [ОРИГИНАЛ] запрета движения у оригинала НЕТ: все
	//   пять читателей зовут заглушку `0xECCF30` (`xor al,al` → 0), флаг никто не читает (отчёт §4.3–4.5). Видимое действие —
	//   только стоп на месте в зоне и плашки 108141/108142; так и переносим.
	UGameInstance* GI = GetGameInstance();
	if (!bOn)
	{
		bMapBlock = false;
		const FString T = Loc(108142);   // «Ограничений на перемещения не будет.» — всем, розовый, только плашка
		AsdaNotice::Plank(GI, EAsdaMsgCategory::System, T, AsdaNoticeColor::Pink, (float)AsdaGmOrigUi::kTtl);
		AddLine(T);
		return;
	}
	AAsdaPlayerCharacter* Ch = SelfChar();
	const FVector2D Pos = Ch ? Ch->GetGamePosXZ() : FVector2D(-1.f, -1.f);
	const int32 Zone = Ch ? MapBlockZoneAt(Pos) : -1;
	if (Zone < 0)
	{
		bMapBlock = false;   // `set_oC(ctx, 0)` и выход без нотиса
		return;
	}
	MapBlockZone = Zone;
	bMapBlock = true;
	// Стоп на месте: 4008 (уходит только если шёл), узлы маршрута, метка земли, автопуть, позиция = текущая, покой.
	UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
	if (Net && Net->bInGameConnected) { Net->SendStop4008((float)Pos.X, (float)Pos.Y); }
	if (Net) { Net->SelfTileX = (int32)(Pos.X / AsdaCoord::kTileUnits); Net->SelfTileZ = (int32)(Pos.Y / AsdaCoord::kTileUnits); }
	if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GI->GetFirstLocalPlayerController(GI->GetWorld())))
	{
		PC->CancelRoute();
		PC->bMovingToPoint = false;
	}
	if (UAsdaMinimapSubsystem* Mm = GI->GetSubsystem<UAsdaMinimapSubsystem>()) { Mm->CancelTravelRequest(); }
	AsdaDestMarkClear(GI, TEXT("6147 стоп на месте (network.c:11524)"));   // ★ 29.10.2026 [ОРИГИНАЛ]: `g_GroundTargetMarkerActive = 0`
	Ch->StopMove();
	const FString T = Loc(108141);   // «Выход за пределы поля боя будет запрещен.» — розовый, только плашка
	AsdaNotice::Plank(GI, EAsdaMsgCategory::System, T, AsdaNoticeColor::Pink, (float)AsdaGmOrigUi::kTtl);
	AddLine(T);
	UE_LOG(LogAsdaGmOrigUi, Log, TEXT("[GM-ориг] 6147: блок, зона %d, стоп в (%.0f,%.0f)"), Zone, Pos.X, Pos.Y);
}
