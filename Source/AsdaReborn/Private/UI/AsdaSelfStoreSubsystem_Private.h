// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSelfStoreSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaSelfStoreSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaSelfStoreSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaSelfStoreSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaSelfStoreSubsystem.h"

#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton
#include "UI/AsdaExchangeSubsystem.h"         // ★ пачка 20, жалоба 10: рынок 13000 открыт — лавка не открывается
#include "UI/AsdaJuiWindowHandle.h"         // каркас окна JUI (Ensure + зона рамки)
#include "UI/AsdaWindowSubsystem.h"         // окна, ESC, колесо, нумпад
#include "UI/AsdaNotice.h"                  // нотисы: msgbox 1500, плашка+чат
#include "UI/AsdaIconCellGrid.h"            // иконки предметов (общая точка)
#include "UI/AsdaItemTooltipSubsystem.h"    // подсказки предметов
#include "UI/AsdaTipPanel.h"                // EAsdaTipOwner::SelfStore
#include "UI/AsdaInventorySubsystem.h"      // ячейки, золото, закрытие сумки, записи 57 б
#include "UI/AsdaPremiumShopSubsystem.h"    // гейт 6300: элитный магазин (`helper_getField2075(g_pShopWnd)`)
#include "UI/AsdaCraftSubsystem.h"          // гейт 6300: окна ремесла (`g_pItemMainWnd` / `g_pItemQtyMakeDlg`)
#include "UI/AsdaChatWindowSubsystem.h"     // мат-фильтр `Chat_MatchBannedWord` (общая точка `IsCursed`)
#include "Npc/AsdaNpcDialogSubsystem.h"     // `g_bInventoryLocked` = открыт разговор с NPC (гейт 6302)
#include "World/AsdaPlayerController.h"     // поза 107/109 (`SendStallStatePose`), «мёртв»
#include "AsdaBytes.h"

#include "Components/EditableText.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"              // `DefaultAsdaTrade.ini` — коды правила «цена лота» сервера

// Категория журнала частей AsdaSelfStoreSubsystem*.cpp: определение — AsdaSelfStoreSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaSelfStore, Log, All );

// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): предел колеса 22199 по вкладке. Вкладка 0 — эталонные 5 (`SetScrollRange(22199, 4, 9)`,
//   расходники 60); вкладка 1 — рядов сумки сверх видимых пяти: 0 при сумке 30 (как эталон — колесо скрыто), 5 при 60.
static int32 AsdaSellScrollMax(int32 Tab)
{
	if (Tab == 0) { return UAsdaSelfStoreSubsystem::kScrollMax; }
	return FMath::Max(0, FMath::DivideAndRoundUp(UAsdaInventorySubsystem::GetShellCapacity(), UAsdaSelfStoreSubsystem::kGridCols) - 5);
}

namespace {
	const FName  kSellId(TEXT("StreetStall_Open_Win"));   // id окна 22000 в реестре окон (как в ESC-таблице эталона)

	const FName  kShopId(TEXT("StreetStall_Win"));        // id окна 23000

	// Цвета надписей эталона (`CControl_SetField923`).
	const FColor kWhite(0xFF, 0xFF, 0xFF, 0xFF);   // −1 — количество
	const FColor kYellow(0xFF, 0xFF, 0x00, 0xFF);  // −256 (0xFFFFFF00) — «+N%», итог витрины, строка 107010
	const FColor kDimText(0xFF, 0xFF, 0xFF, 0x7D); // 2113929215 (0x7DFFFFFF) — надпись выставленного/распроданного
	constexpr float kDimAlpha = 125.f / 255.f;     // `CControl_SetColor4(ctrl, 0x7D, 0)` — иконка выставленного
}

namespace {
namespace L {
// loc-строки (все — из эталонных вызовов; тексты — `RawAssets/Text/GameText.csv`).
		constexpr int32 kNameTitle = 105000, kNameSave = 105001, kHdrSell = 105002, kHdrBag = 105003, kHdrPrice = 105004,
			kHdrQty = 105005, kHdrTotal = 105006, kTab0 = 105007, kTab1 = 105008, kSumCap = 105009, kGoldCap = 105010,
			kOk = 105011, kCancel = 105012;
		constexpr int32 kColList = 107000, kColUnit = 107001, kColTotal = 107002, kCartLbl = 107003, kBuyTotal = 107004,
			kMoney = 107005, kBtnOk = 107006, kBtnCancel = 107007, kSellTotal = 107009, kListFull = 107010,
			kReady0 = 107021, kReady2 = 107022, kReady3 = 107023, kReady4 = 107024, kClose0 = 107030, kClose2 = 107031,
			kOpen0 = 107040, kOpen2 = 107042, kEdit = 107060, kDefName = 107061, kNoName = 107062, kLowLevel = 107080,
			kNothing = 107085, kCurse = 9005, kReady5 = 112500, kReady6 = 112501, kReady7 = 112502;
		// ── посетитель (волна Л2): обработчики — `shopHandleOpenShopPacket`, `shopCart_addItem`, `CShopSellDlg_RefreshTotalPrice`,
		//   `CShopDlg_OnCommand` (23305), `shopSellDlg_OnSellResult` (6310), 6311/6312 (107082), 6315 (107087/107088).
		constexpr int32 kVisit0 = 107070, kVisit2 = 107072, kCartFull = 107086, kNoGold = 107056, kBuyEmpty = 107084,
			kNoAnswer = 107081, kBuyAsk = 6888, kBuyAskOk = 12519, kBuyAskCancel = 2424, kBuy0 = 107050, kBuy9 = 107048,
			kSoldLog = 107082, kGuestIn = 107087, kGuestOut = 107088;
}
}

// Ключ CVarFixK5K6: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarFixK5K6;
#else
extern TAsdaConstCVar<int32> CVarFixK5K6;
#endif

// Ключ CVarStrayGuard: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarStrayGuard;
#else
extern TAsdaConstCVar<int32> CVarStrayGuard;
#endif

namespace {
	// Строка с разделителями тысяч (`String_FormatThousands`) для int64 — [ПОРТ] суммы К-5/К-6.
	FString Thousands64(int64 V)
	{
		const bool bNeg = V < 0;
		const FString Digits = FString::Printf(TEXT("%lld"), bNeg ? -V : V);
		FString Out;
		const int32 N = Digits.Len();
		for (int32 i = 0; i < N; ++i)
		{
			Out.AppendChar(Digits[i]);
			const int32 Left = N - 1 - i;
			if (Left > 0 && (Left % 3) == 0) { Out.AppendChar(TEXT(',')); }
		}
		return bNeg ? (TEXT("-") + Out) : Out;
	}

	void SetTxt(UTextBlock* T, const FString& S) { if (T) { T->SetText(FText::FromString(S)); } }
	void SetTxtColor(UTextBlock* T, const FColor& C) { if (T) { T->SetColorAndOpacity(FSlateColor(FLinearColor::FromSRGBColor(C))); } }
	void SetEdit(UEditableText* E, const FString& S) { if (E) { E->SetText(FText::FromString(S)); } }
	void SetImgAlpha(UImage* I, float A) { if (I) { FLinearColor C = I->GetColorAndOpacity(); C.A = A; I->SetColorAndOpacity(C); } }

	// `slotStructReset@0xDD39B0` — лот 45 б по умолчанию (@5 −1, @11 −1, @17 4×i16 −1, прочее 0).
	TArray<uint8> MakeResetLot45()
	{
		TArray<uint8> R; R.SetNumZeroed(45);
		AsdaBytes::WrI16(R.GetData() + 5, -1);
		AsdaBytes::WrI16(R.GetData() + 11, -1);
		for (int32 k = 0; k < 4; ++k) { AsdaBytes::WrI16(R.GetData() + 17 + 2 * k, -1); }
		return R;
	}
}
