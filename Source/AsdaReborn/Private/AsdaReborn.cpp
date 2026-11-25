// Copyright (c) 2026 @sitxovski (Discord)
#include "Modules/ModuleManager.h"
#include "Text/AsdaLang.h"
#include "Crypto/AsdaPakKeyProvider.h"
#include "Crypto/AsdaPakIntegrity.h"

// ★ [ПОРТ] 09.10.2026 (мультиязычность): свой класс игрового модуля вместо `FDefaultGameModuleImpl` — ради ОДНОЙ
//   точки: язык сессии и культура UE выставляются в `StartupModule`, то есть ДО загрузки первой карты и первых
//   пакетов интерфейса. Тогда локализованные пакеты (`/Game/L10N/<культура>/…`) движок подменяет с самого начала.
//   Подробно — шапка `Public/Text/AsdaLang.h`.
class FAsdaRebornGameModule : public FDefaultGameModuleImpl
{
public:
	// Старт модуля: определить язык сессии (командная строка → профиль → русский) и выставить культуру.
	virtual void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();
		FAsdaLang::InitAtStartup();
	}
};

// ★★★ ВОЛНА В2 АНТИЧИТА (12.10.2026): ключи pak/IoStore НЕ встраиваются движком открытым текстом.
//   UBT читает Config/DefaultCrypto.ini и задаёт для модулей проекта макросы
//   IMPLEMENT_ENCRYPTION_KEY_REGISTRATION()=UE_REGISTER_ENCRYPTION_KEY(0x..,…) и IMPLEMENT_SIGNING_KEY_REGISTRATION()
//   (TargetRules.cs:3434–3459, UEBuildModuleCPP.cs:2831); раскрываются они ТОЛЬКО внутри IMPLEMENT_PRIMARY_GAME_MODULE
//   (ModuleManager.h:1009–1133) — 32 байта ключа AES в .exe как есть. Снимаем оба макроса и регистрируем те же
//   колбэки движка (CoreDelegates.cpp:393–406) сами: ключ собирается из замаскированных долей в памяти
//   (AsdaPakKey::RestoreEncryptionKey, Source/AsdaCore), открытый ключ подписи — из того же набора.
//   Регистрант СТАТИЧЕСКИЙ и в этом же TU — как у движка: делегаты должны быть привязаны до
//   FPakPlatformFile::Initialize (PreInit, раньше StartupModule любого модуля).
#undef IMPLEMENT_ENCRYPTION_KEY_REGISTRATION
#define IMPLEMENT_ENCRYPTION_KEY_REGISTRATION()
#undef IMPLEMENT_SIGNING_KEY_REGISTRATION
#define IMPLEMENT_SIGNING_KEY_REGISTRATION()

namespace
{
	// Статический регистрант ключей сборки: шифрование, подпись, обработчик отказа подписи.
	struct FAsdaPakKeyRegistration
	{
		FAsdaPakKeyRegistration()
		{
			if (!AsdaPakKey::IsConfigured())
			{
				return; // Набор ключей не сгенерирован (Tools/gen_build_crypto.py) — архивы без крипто.
			}
			extern CORE_API void RegisterEncryptionKeyCallback(void (*)(unsigned char OutKey[32]));
			RegisterEncryptionKeyCallback(&AsdaPakKey::RestoreEncryptionKey);
			if (AsdaPakKey::IsSigningEnabled())
			{
				extern CORE_API void RegisterSigningKeyCallback(void (*)(TArray<uint8>&, TArray<uint8>&));
				RegisterSigningKeyCallback(&AsdaPakKey::GetSigningPublicKey);
				AsdaPakIntegrity::Register();
			}
		}
	} GAsdaPakKeyRegistration;
}

IMPLEMENT_PRIMARY_GAME_MODULE(FAsdaRebornGameModule, AsdaReborn, "AsdaReborn");
