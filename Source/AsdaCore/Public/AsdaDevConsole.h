// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ДЕВ-КЛЮЧИ И ДЕВ-КОМАНДЫ КОНСОЛИ: ОДНА ТОЧКА (античит, волна А2 «клиентская гигиена», мера 6; 12.10.2026). [ПОРТ]
//
// ЗАЧЕМ. Консоли в Shipping нет, но любой НЕ-читерский консольный ключ ставится игроком без консоли — строкой в
//   пользовательском `Saved/Config/<платформа>/Engine.ini [SystemSettings]`, а команды доступны инжектированному коду.
//   Ключи, меняющие геймплей или форму исходящих пакетов (темп атаки, ход, GM-признаки, XyT, ключи отката `*Legacy`),
//   и все команды (самотесты, стенды, подделки входящих, отправка пакетов из консоли) в Shipping НЕ РЕГИСТРИРУЮТСЯ:
//   ни имени ключа, ни текста справки в бинаре нет, значит и ini их задать не может.
//   Отчёт и перепись всех ключей: `Reports/anticheat/wave_A2_client.md`, план — `Reports/anticheat/00_PLAN.md` §2 мера 6.
//
// КАК ПОЛЬЗОВАТЬСЯ (новый ключ — сразу так; графические/звуковые настройки игрока остаются обычными `TAutoConsoleVariable`):
//   static ASDA_DEV_CVAR(int32, CVarFoo, TEXT("asda.Foo"), 1, TEXT("справка"), ECVF_Default);
//       Development/Editor: обычный `TAutoConsoleVariable<int32> CVarFoo(TEXT("asda.Foo"), 1, TEXT("справка"), ECVF_Default)`.
//       Shipping: `TAsdaConstCVar<int32> CVarFoo(1)` — константа с умолчанием; имя и справка вырезаны препроцессором.
//       Чтение в коде то же самое: `CVarFoo.GetValueOnGameThread()`, `CVarFoo->GetInt()`, `CVarFoo->Set(…)` (внутри процесса).
//   static ASDA_DEV_CVAR_REF(CVarFooRef, TEXT("asda.Foo"), GFoo, TEXT("справка"), ECVF_Default);
//       Development: `FAutoConsoleVariableRef`; Shipping: пустой объект, `GFoo` остаётся со своим начальным значением.
//   Команды: регистрацию целиком заключать в `#if ASDA_DEV_CONSOLE … #endif` (тела лямбд с запятыми и `#if` внутри
//       нельзя передавать аргументом макроса).
//   Поиск чужого дев-ключа по имени: `ASDA_FIND_DEV_CVAR(TEXT("asda.Foo"))` — в Shipping это `nullptr` без строки имени;
//       ветка «ключа нет» у вызывающего обязана давать то же, что умолчание ключа.
//   Сообщение (не лог), где названо имя дев-ключа: `ASDA_DEV_TEXT(TEXT("… (asda.Foo 0)"), TEXT("…"))` — в Shipping второй текст.
#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include <type_traits>

// 1 — дев-ключи и дев-команды регистрируются (Debug/Development/Test, редактор); 0 — Shipping.
#ifndef ASDA_DEV_CONSOLE
	#define ASDA_DEV_CONSOLE (!UE_BUILD_SHIPPING)
#endif

// Константа на месте консольного ключа в Shipping: то же чтение, что у `TAutoConsoleVariable<T>`, но без регистрации.
//   Значение — умолчание ключа; `Set` из кода игры меняет его ВНУТРИ процесса (снаружи — ini/консоль — достучаться нечем).
template <typename T>
class TAsdaConstCVar
{
public:
	// Прокладка для `->`/`AsVariable()`: подмножество `IConsoleVariable`, которым пользуется код порта.
	class FView
	{
	public:
		explicit FView(TAsdaConstCVar* InOwner) : Owner(InOwner) {}
		int32 GetInt() const { return Owner->AsInt(); }                                       // целое значение
		float GetFloat() const { return Owner->AsFloat(); }                                   // вещественное значение
		bool GetBool() const { return Owner->AsInt() != 0; }                                  // логическое значение
		FString GetString() const { return Owner->AsString(); }                               // значение строкой
		void Set(int32 V, EConsoleVariableFlags = ECVF_SetByCode) { Owner->FromInt(V); }     // запись целым
		void Set(float V, EConsoleVariableFlags = ECVF_SetByCode) { Owner->FromFloat(V); }   // запись вещественным
		void Set(bool V, EConsoleVariableFlags = ECVF_SetByCode) { Owner->FromInt(V ? 1 : 0); }   // запись логическим
		void Set(const TCHAR* V, EConsoleVariableFlags = ECVF_SetByCode) { Owner->FromString(V); }   // запись строкой
	private:
		TAsdaConstCVar* Owner;   // константа-владелец
	};

	explicit TAsdaConstCVar(const T& InValue) : Value(InValue), View(this) {}
	TAsdaConstCVar(const TAsdaConstCVar&) = delete;
	TAsdaConstCVar& operator=(const TAsdaConstCVar&) = delete;

	T GetValueOnGameThread() const { return Value; }               // как у TAutoConsoleVariable
	T GetValueOnRenderThread() const { return Value; }             // как у TAutoConsoleVariable
	T GetValueOnAnyThread(bool = false) const { return Value; }    // как у TAutoConsoleVariable
	FView* operator->() { return &View; }                          // `CVar->GetInt()` / `CVar->Set(…)`
	const FView* operator->() const { return &View; }              // то же для константного доступа
	FView* AsVariable() { return &View; }                          // `CVar.AsVariable()->Set(…)`

private:
	int32 AsInt() const
	{
		if constexpr (std::is_same_v<T, FString>) { return FCString::Atoi(*Value); }
		else { return (int32)Value; }
	}
	float AsFloat() const
	{
		if constexpr (std::is_same_v<T, FString>) { return FCString::Atof(*Value); }
		else { return (float)Value; }
	}
	FString AsString() const { return LexToString(Value); }
	void FromInt(int32 V)
	{
		if constexpr (std::is_same_v<T, FString>) { Value = LexToString(V); }
		else if constexpr (std::is_same_v<T, bool>) { Value = (V != 0); }
		else { Value = (T)V; }
	}
	void FromFloat(float V)
	{
		if constexpr (std::is_same_v<T, FString>) { Value = LexToString(V); }
		else if constexpr (std::is_same_v<T, bool>) { Value = (V != 0.f); }
		else { Value = (T)V; }
	}
	void FromString(const TCHAR* V) { LexFromString(Value, V); }

	T Value;     // текущее значение (умолчание ключа)
	FView View;  // прокладка для `->`
};

// Пустышка на месте `FAutoConsoleVariableRef` в Shipping: связанная глобальная переменная остаётся с начальным значением.
struct FAsdaNoCVarRef
{
};

#if ASDA_DEV_CONSOLE
	// Development: обычная регистрация ключа (аргументы — ровно как у конструктора `TAutoConsoleVariable<Type>`).
	#define ASDA_DEV_CVAR(Type, Var, ...) TAutoConsoleVariable<Type> Var(__VA_ARGS__)
	// Development: обычная привязка ключа к глобальной переменной (аргументы — как у `FAutoConsoleVariableRef`).
	#define ASDA_DEV_CVAR_REF(Var, ...) FAutoConsoleVariableRef Var(__VA_ARGS__)
	// Development: поиск ключа по имени.
	#define ASDA_FIND_DEV_CVAR(Name) IConsoleManager::Get().FindConsoleVariable(Name)
	// Development: текст сообщения с именем дев-ключа (для журнала/причин отказа).
	#define ASDA_DEV_TEXT(DevText, ShipText) DevText
#else
	// Shipping: константа с умолчанием; имя, справка и флаги вырезаны.
	#define ASDA_DEV_CVAR(Type, Var, Name, Default, ...) TAsdaConstCVar<Type> Var(Default)
	// Shipping: ключа нет, переменная остаётся с начальным значением.
	#define ASDA_DEV_CVAR_REF(Var, ...) FAsdaNoCVarRef Var
	// Shipping: ключа нет (строки имени в бинаре нет).
	#define ASDA_FIND_DEV_CVAR(Name) static_cast<IConsoleVariable*>(nullptr)
	// Shipping: тот же текст без имени ключа (ключа нет — упоминать нечего, строки имени в бинаре нет).
	#define ASDA_DEV_TEXT(DevText, ShipText) ShipText
#endif
