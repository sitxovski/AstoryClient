// Copyright (c) 2026 @sitxovski (Discord)
// Общая точка «в корзину — двойным щелчком» (описание — в `UI/AsdaShopDblClick.h`).
#include "UI/AsdaShopDblClick.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

// [ПОРТ] Ключ отката требования владельца 10.10.2026: 1 — в корзину двойным щелчком, 0 — одиночным (как было).
static ASDA_DEV_CVAR(int32, CVarShopDblClickToCart,
	TEXT("asda.Shop.DblClickToCart"), 1,
	TEXT("1 — во всех магазинах товар кладётся в корзину/список покупки ДВОЙНЫМ щелчком, одиночный только выделяет ")
	TEXT("(требование владельца 10.10.2026, [ПОРТ]); 0 — прежнее поведение: кладёт одиночный щелчок."),
	ECVF_Default);

// ★ Дедуп №2, В2-A (U1, 11.10.2026): ключ отката единого порога двойного щелчка. Описание — у `FAsdaDblClick` в заголовке.
static ASDA_DEV_CVAR(int32, CVarDblClickLegacy,
	TEXT("asda.UI.DblClickLegacy"), 0,
	TEXT("0 — двойной щелчок во всех окнах по системному порогу Windows (`GetDoubleClickTime`), реальное время ")
	TEXT("(решение владельца 11.10.2026, как у оригинала WM_LBUTTONDBLCLK); 1 — прежние пороги окон: сумка и разборка 0,2 с ")
	TEXT("(UInputSettings::DoubleClickTime), склад и сброс 0,35 с, развитие питомца 0,5 с, мини-статус 0,5 с игрового времени."),
	ECVF_Default);

bool FAsdaShopDblClick::IsEnabled()
{
	return CVarShopDblClickToCart.GetValueOnGameThread() != 0;
}

bool FAsdaDblClick::IsLegacy()
{
	return CVarDblClickLegacy.GetValueOnGameThread() != 0;
}

double FAsdaDblClick::WindowSec()
{
#if PLATFORM_WINDOWS
	return (double)::GetDoubleClickTime() / 1000.0;
#else
	return 0.5;   // системное значение Windows по умолчанию
#endif
}

bool FAsdaDblClick::ClickAt(int32 A, int32 B, double Now, double Window)
{
	if (A < 0) { Reset(); return false; }         // щелчок мимо узла — пара рвётся
	const bool bSecond = (LastA == A) && (LastB == B) && (LastTime >= 0.0) && ((Now - LastTime) <= Window);
	if (bSecond) { Reset(); return true; }        // ровно один раз: третий щелчок снова первый
	LastA = A; LastB = B; LastTime = Now;
	return false;
}

bool FAsdaDblClick::Click(int32 A, int32 B)
{
	return ClickAt(A, B, FPlatformTime::Seconds(), EffectiveSec());
}

bool FAsdaDblClick::ProbePair(double DtSec) const
{
	FAsdaDblClick P;                       // копия без состояния пары: порог экземпляра, свой ключ узла
	P.LegacySec = LegacySec;
	const double Win = EffectiveSec();
	P.ClickAt(0, 0, 1000.0, Win);
	return P.ClickAt(0, 0, 1000.0 + DtSec, Win);
}

bool FAsdaShopDblClick::Click(int32 A, int32 B)
{
	if (!IsEnabled()) { Reset(); return true; }   // режим выключен — каждый щелчок «кладёт», как до правки
	// Порог магазинов — системный всегда (`LegacySec` у них не задан): прежняя формула 1:1.
	return FAsdaDblClick::Click(A, B);
}
