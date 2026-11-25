// Copyright (c) 2026 @sitxovski (Discord)
// ★ R4-c (14.11.2026) [ПОРТ]: ШАБЛОН КЛАССА (CDO) НЕ ТИКАЕТ — общая точка для подсистем `FTickableGameObject`.
//
// ЧТО БЫЛО. Подсистема порта объявлена как `UGameInstanceSubsystem, public FTickableGameObject`. Конструктор
//   `FTickableGameObject` ставит в список тика КАЖДЫЙ объект класса, в том числе шаблон класса (CDO), а движок шаблон от
//   рабочего объекта не отличает (так делает только `UTickableWorldSubsystem`). Поэтому `Tick` звался дважды за кадр: у
//   рабочей подсистемы и у шаблона. Шаблон работает со своими пустыми полями (очереди пусты, окна закрыты, GameInstance
//   нет) и ничего полезного не делает. Замер 14.11 (`asda.Perf.Series`, 900 кадров): у подсистем без защиты — 1800 вызовов,
//   у `UAsdaPartySubsystem` и `UAsdaTutorialSubsystem` (там `IsTickable` уже возвращал `!IsTemplate()`) — 900.
// ЧТО СТАЛО. В заголовке подсистемы: `virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }`.
//   Рабочая подсистема тикает как раньше — её очереди пакетов и ленивые подписки не затронуты.
// ★ R4-c (15.11.2026, закрытие): ключ `asda.Perf.NoCdoTick`, прежняя ветка (шаблон тикает) и самотест равенства `asda.Perf.CdoTickSelfTest` убраны —
//   шаблон не тикает всегда. Упоминания ключа в заголовках подсистем остались как история правки.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

namespace AsdaTickGate
{
	// Можно ли объекту тикать: шаблон класса — нет, рабочий объект — да.
	FORCEINLINE bool NotCdo(const UObject* O)
	{
		return !O->HasAnyFlags(RF_ClassDefaultObject);
	}
}
