// Copyright (c) 2026 @sitxovski (Discord)
#include "UI/AsdaNotice.h"
#include "UI/AsdaUiHelpers.h"        // AsdaUi::LocRaw — сырая строка через общий форматтер
#include "UI/AsdaWindowSubsystem.h"  // ShowGameMessage — однокнопочное окно 1500
#include "UI/AsdaMessageSubsystem.h" // маршрутизатор ленты
#include "Text/AsdaTextSubsystem.h"  // loc-строки и слоты подстановки
#include "Engine/GameInstance.h"
#include "Engine/World.h"            // ★ U13: ConsoleEcho берёт GameInstance из мира команды
#include "AsdaDevConsole.h"           // ★ 30.10.2026: ASDA_DEV_CVAR — ключ asda.Notice.MsgBoxKeepTags

namespace
{
	// Подсистема GameInstance из константного указателя (сами подсистемы состояния GI не меняют).
	template <typename T>
	T* Sub(const UGameInstance* GI)
	{
		return GI ? const_cast<UGameInstance*>(GI)->GetSubsystem<T>() : nullptr;
	}
}

void AsdaNotice::ConsoleEcho(UWorld* W, const FString& Text, const FLogCategoryBase& LogCategory)
{
	// Тело — построчно прежних `XxxEcho`: строки без отбрасывания пустых; в ленту — только непустые.
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;
	TArray<FString> Lines;
	Text.ParseIntoArray(Lines, TEXT("\n"), /*CullEmpty=*/false);
	for (const FString& L : Lines)
	{
		UE_LOG_REF(LogCategory, Display, TEXT("%s"), *L);
		if (Msg && !L.TrimStartAndEnd().IsEmpty()) { Msg->PostText(EAsdaMsgCategory::Tip, L); }
	}
}

FString AsdaNotice::Loc(const UGameInstance* GI, int32 Id)
{
	const UAsdaTextSubsystem* T = Sub<UAsdaTextSubsystem>(GI);
	return T ? T->GetTextById(Id) : FString();
}

FString AsdaNotice::LocRaw(const UGameInstance* GI, int32 Id, bool bDebugFallback)
{
	return AsdaUi::LocRaw(GI, Id, bDebugFallback);
}

FString AsdaNotice::LocTagged(const UGameInstance* GI, int32 Id)
{
	const UAsdaTextSubsystem* T = Sub<UAsdaTextSubsystem>(GI);
	return T ? T->GetTextByIdRaw(Id) : FString();
}

FString AsdaNotice::LocSlot(const UGameInstance* GI, int32 Id, const FString& Slot1)
{
	UAsdaTextSubsystem* T = Sub<UAsdaTextSubsystem>(GI);
	if (!T) { return FString(); }
	T->NoticeSetString(0, Slot1);
	return T->FormatNotice(Id);
}

bool AsdaNotice::Chat(const UGameInstance* GI, EAsdaMsgCategory Cat, int32 LocId, const FString& Fallback)
{
	UAsdaMessageSubsystem* Msg = Sub<UAsdaMessageSubsystem>(GI);
	if (!Msg) { return false; }
	Msg->PostLoc(Cat, LocId, Fallback);
	return true;
}

bool AsdaNotice::ChatText(const UGameInstance* GI, EAsdaMsgCategory Cat, const FString& Text)
{
	if (Text.IsEmpty()) { return false; }
	UAsdaMessageSubsystem* Msg = Sub<UAsdaMessageSubsystem>(GI);
	if (!Msg) { return false; }
	Msg->PostText(Cat, Text);
	return true;
}

bool AsdaNotice::ChatFmt(const UGameInstance* GI, EAsdaMsgCategory Cat, int32 LocId,
	const TArray<FString>& StringArgs, const TArray<int32>& NumberArgs)
{
	UAsdaMessageSubsystem* Msg = Sub<UAsdaMessageSubsystem>(GI);
	if (!Msg) { return false; }
	Msg->PostLocFmt(Cat, LocId, StringArgs, NumberArgs);
	return true;
}

bool AsdaNotice::ChatColored(const UGameInstance* GI, EAsdaMsgCategory Cat, const FString& Text,
	FColor Color, float TtlMs)
{
	if (Text.IsEmpty()) { return false; }
	UAsdaMessageSubsystem* Msg = Sub<UAsdaMessageSubsystem>(GI);
	if (!Msg) { return false; }
	FAsdaMsgParams P;
	P.Category  = Cat;
	P.Text      = Text;
	P.Targets   = (int32)EAsdaMsgTarget::SystemChat;   // только журнал + буфер A — ровно `AddSystemMessage`
	P.bUseColor = true;
	P.Color     = Color;
	P.TtlMs     = TtlMs;
	Msg->Post(P);
	return true;
}

bool AsdaNotice::Plank(const UGameInstance* GI, EAsdaMsgCategory Cat, const FString& Text, FColor Color, float TtlMs,
	int32 SrcId)
{
	// Разбор у объявления. Поля — ровно те, что ставили все три прежние копии (склад/сумка/ремесло).
	if (Text.IsEmpty()) { return false; }
	UAsdaMessageSubsystem* Msg = Sub<UAsdaMessageSubsystem>(GI);
	if (!Msg) { return false; }
	FAsdaMsgParams P;
	P.Category  = Cat;
	P.Text      = Text;
	P.Targets   = (int32)EAsdaMsgTarget::WarWindow;   // 221, а не журнал чата
	P.bUseColor = true;
	P.Color     = Color;
	P.TtlMs     = TtlMs;
	P.SrcOpcode = SrcId;
	Msg->Post(P);
	return true;
}

bool AsdaNotice::PlankLoc(const UGameInstance* GI, EAsdaMsgCategory Cat, int32 LocId, FColor Color, float TtlMs,
	int32 SrcId)
{
	// Разбор у объявления: текст — `Loc` (теги срезаны, как у всех пользователей плашки), фолбэк — плейсхолдер в ленте.
	const FString S = Loc(GI, LocId);
	if (S.IsEmpty()) { Chat(GI, Cat, LocId); return false; }
	return Plank(GI, Cat, S, Color, TtlMs, SrcId);
}

bool AsdaNotice::PlankAndChatLoc(const UGameInstance* GI, EAsdaMsgCategory Cat, int32 LocId, FColor Color, float TtlMs,
	int32 SrcId, bool bChatFirst)
{
	// Разбор у объявления. Порядок задаёт вызывающий (`bChatFirst`) — у эталона он разный у 5055/6201 и у ремесла.
	const FString S = Loc(GI, LocId);
	if (S.IsEmpty()) { Chat(GI, Cat, LocId); return false; }
	bool bChat = false;
	if (bChatFirst) { bChat = ChatColored(GI, Cat, S, Color, TtlMs); }
	const bool bPlank = Plank(GI, Cat, S, Color, TtlMs, SrcId);
	if (!bChatFirst) { bChat = ChatColored(GI, Cat, S, Color, TtlMs); }
	return bPlank || bChat;
}

bool AsdaNotice::MsgBox(const UGameInstance* GI, EAsdaMsgCategory Cat, const FString& Text, int32 OkLocId,
	int32 EtalonNoticeId)
{
	if (Text.IsEmpty()) { return false; }
	if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(GI))
	{
		// true и при отброшенном дубле (как эталон: `CItem_BuildTooltipText` результата не возвращает). ⚠ Для пары
		//   (0, −1) игрок при этом видит ДРУГОЕ сообщение с той же парой, ЭТОТ текст теряется (паритет с оригиналом,
		//   ревизия М-4) — фолбэк вызывающего в чат по `false` здесь не сработает.
		WM->ShowGameMessage(Text, OkLocId, Cat, nullptr, 0, EtalonNoticeId);
		return true;
	}
	ChatText(GI, Cat, Text);   // окна нет — отказ игрок всё равно видит
	return false;
}

// ★★ [ОРИГИНАЛ] 30.10.2026 (волна bugs8, баг «сырые теги / нет цвета в текстах»): эталон отдаёт окну-сообщению
//   `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, UI_FormatLocalizedNotice(id), 1)` строку С ТЕГАМИ — тело окна
//   рисует `<FC=…>` цветом (7006 game_main.c:6355, 110114 ui_core.c:48331, 110119 network.c:19789…). Порт брал `Loc`
//   (теги срезаны) — цвет терялся, хотя ONEBUT/TWOBUT/GIFT с 27.09 выводят тело через `ApplyFormatted`.
//   1 = текст с тегами (`LocTagged`: маркеры как у `Loc`), 0 = прежний срез тегов.
static ASDA_DEV_CVAR(int32, CVarMsgBoxKeepTags,
	TEXT("asda.Notice.MsgBoxKeepTags"), 1,
	TEXT("1 = окно-сообщение по loc-id получает строку с тегами цвета (как оригинал); 0 = теги срезаются (прежний порт)."),
	ECVF_Default);

bool AsdaNotice::MsgBoxLoc(const UGameInstance* GI, EAsdaMsgCategory Cat, int32 LocId, int32 OkLocId,
	int32 EtalonNoticeId)
{
	const FString S = (CVarMsgBoxKeepTags.GetValueOnGameThread() != 0) ? LocTagged(GI, LocId) : Loc(GI, LocId);
	UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(GI);
	if (WM && !S.IsEmpty())
	{
		WM->ShowGameMessage(S, OkLocId, Cat, nullptr, 0, EtalonNoticeId);
		return true;
	}
	Chat(GI, Cat, LocId);
	return false;
}

UUserWidget* AsdaNotice::MsgBoxCb(const UGameInstance* GI, EAsdaMsgCategory Cat, const FString& Text,
	TFunction<void()> OnOk, int32 ZOrder, int32 OkLocId, int32 EtalonNoticeId)
{
	// Разбор у объявления: та же политика фолбэка, что у `MsgBox` (окна нет ⇒ лента категорией вызывающего).
	if (Text.IsEmpty()) { return nullptr; }
	if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(GI))
	{
		return WM->ShowGameMessage(Text, OkLocId, Cat, MoveTemp(OnOk), ZOrder, EtalonNoticeId);
	}
	ChatText(GI, Cat, Text);
	return nullptr;
}
