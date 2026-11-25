<div align="center">

[English](README.md) · [Русский](README.ru.md) · **العربية**

### ❤️ هل أعجبك المشروع؟ ادعم هذا العمل الشاق على [**Boosty**](https://boosty.to/sitxovski_yurii)

# Astory Client

**عميل لعبة Astory — إعادة إنتاج للعبة MMORPG كلاسيكية على محرك Unreal Engine 5.8**

[Astory Client](https://github.com/sitxovski/AstoryClient) · [Astory Server](https://github.com/sitxovski/AstoryServer) · [Astory Admin Center](https://github.com/sitxovski/AstoryAdminCenter)

</div>

---

<div dir="rtl">

> ⚠️ تنبيه. هذه الحزمة وحدها (وليس المشروع بأكمله) أُعدّت باستخدام Claude Code، وقد تحتوي على بعض الأخطاء. أما المشروع نفسه فقد طُوِّر يدويًا بالكامل. للحصول على المساعدة، راسلني على Discord ‏(@sitxovski).

## محتويات المستودع

| المجلد | الوصف |
|---|---|
| `Source` | اللعبة بلغة C++‎ ‏(UE 5.8): الشبكة، والواجهة، والقتال، والعالم، ومكافحة الغش |
| `Config`, `Plugins` | إعدادات المحرك وإضافات المحرّر |
| `Tools` | أدوات بلغة Python: الاستيراد، والبيانات، وتوقيع الإصدارات |
| `Launcher` | مشغّل اللعبة مع التحديث التلقائي (C++ / CMake) |
| `DEPLOY` | سكربتات البناء والتحديث وبرنامج التثبيت بنقرة واحدة |
| `Prerequisites` | ما يجب تثبيته، وكيفية البناء والتشغيل |

> 📦 **موارد اللعبة تُنزَّل بشكل منفصل.** نزّل [**AstoryClient Resources**](https://drive.google.com/drive/folders/1p9x3ekIwU1PdqFaRMMeuR9ugiUC9DaT9?usp=sharing)
> وانسخ مجلد `Content` إلى هذا المجلد، بحيث يصبح لديك المسار `AstoryClient\Content\AsdaAssets\...`.

## البدء السريع

1. **الاستنساخ** — في مجلد اسمه `AstoryClient` بالضبط، بجوار `AstoryServer` و`AstoryAdminCenter` (ملف ZIP من GitHub يُفك باسم `AstoryClient-main`، فأعد تسميته).
2. **التثبيت** — شغّل `Prerequisites\install_prerequisites.bat` ‏([التفاصيل](Prerequisites/README.md)).
3. **الموارد** — ضع مجلد `Content` من [*AstoryClient Resources*](https://drive.google.com/drive/folders/1p9x3ekIwU1PdqFaRMMeuR9ugiUC9DaT9?usp=sharing) في هذا المجلد.
4. **الإعداد** — شغّل `_setup\configure.bat` (خادمك، والعناوين، ومعرّفات Epic — [من أين تحصل عليها](_setup/SETUP.md)).
   شغّله في النسخ الثلاث كلها؛ تُحفظ إجاباتك، فلا تسألك النسخ الأخرى إلا عن الجديد.
5. **البناء** — شغّل `DEPLOY\Build\1_Editor_Binaries.bat`، ثم افتح `AsdaReborn.uproject`.
   انسخ أولًا الملف `server_public.h` الخاص بخادمك إلى `Source\AsdaNet\Private\Secure\` (يُنشئه إعداد [Astory Server](https://github.com/sitxovski/AstoryServer)؛ وإلى أن تفعل ذلك يتوقف البناء مع تلميح).
   قبل أول بناء للإصدار أنشئ مفاتيحك الخاصة (مفاتيح pak، وLogSeal، وEasy Anti-Cheat) — راجع [Prerequisites](Prerequisites/README.md).

يتصل العميل بخادم اللعبة الخاص بك — راجع [Astory Server](https://github.com/sitxovski/AstoryServer).
ولتشغيل اللعبة للاعبين ستحتاج أيضًا إلى [Astory Admin Center](https://github.com/sitxovski/AstoryAdminCenter).

## المؤلف

صاحب المشروع: **@sitxovski** ‏(Discord).

يمكنك استخدامه بحرية لأي غرض ودون أي قيود — [رخصة MIT](LICENSE). **فقط اذكر المؤلف الأصلي — @sitxovski ‏(Discord) —
وضع رابطًا لهذا المستودع عند نسخ أي جزء منه أو تفريعه (fork) أو إعادة استخدامه.**

</div>

---

<div align="center">

### ❤️ ادعم هذا العمل الشاق على [**Boosty**](https://boosty.to/sitxovski_yurii)

</div>
