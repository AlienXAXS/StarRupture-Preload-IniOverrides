// ---------------------------------------------------------------------------
// IniOverrides -- let the game read user INI files on top of its baked config
//
// StarRupture ships its configuration baked into Config/BinaryConfig.ini. At
// boot FConfigCacheIni::InitializeConfigSystem loads that blob, then re-runs
// FConfigContext::Load for every known file (Engine, Game, Input, Scalability,
// GameplayTags, ...) in "fixing up after binary config" mode. That pass skips
// every static layer -- they are already in the blob -- and exists to apply
// exactly one thing on top: the SAVED layer,
//
//     <Saved>\Config\<Platform>\<Name>.ini
//
// which is the file stock UE5 games let you edit. StarRupture was built with
// DISABLE_GENERATED_INI_WHEN_COOKED=1, which makes FConfigContext::PerformLoad
// open with:
//
//     if (Stricmp(BaseIniName, L"GameUserSettings") != 0)
//     {
//         bAllowGeneratedIniWhenCooked = false;     // FConfigContext+0x181
//         Branch->InMemoryFile.NoSave  = true;      // FConfigBranch+0x4A0/0x4C0 |= 2
//     }
//
// GenerateDestIniFile only reads the saved-layer file when that flag is set,
// so every file except GameUserSettings.ini is silently ignored.
//
// The patch rewrites the one store that clears the flag so it sets it
// instead:
//
//     44 88 B7 81 01 00 00    mov [rdi+181h], r14b        ; flag = 0 (r14 is 0)
//  -> C6 87 81 01 00 00 01    mov byte ptr [rdi+181h], 1  ; flag = 1
//
// Same length, no branches moved. The NoSave line below it is left alone on
// purpose: the game reads the user's files but still never writes Engine.ini,
// Game.ini and friends, exactly as it behaves today. Nothing the game already
// does changes except that it now reads those files.
//
// It has to happen here rather than in an ordinary plugin because the config
// system is initialised in FEngineLoop::PreInit, long before any ordinary
// plugin is loaded. Preload runs before the game's entry point, so the bytes
// are patched before PerformLoad ever runs.
//
// No configuration: the DLL being in ModLoader\Preload\ is the switch.
// ---------------------------------------------------------------------------

#include "preload_interface.h"

#include <cstring>

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

#if defined(MODLOADER_CLIENT_BUILD)
#define INIOVERRIDES_TARGET      PRELOAD_TARGET_CLIENT
#define INIOVERRIDES_SAVED_DIR   "%LOCALAPPDATA%\\StarRupture\\Saved\\Config\\Windows\\"
#elif defined(MODLOADER_SERVER_BUILD)
#define INIOVERRIDES_TARGET      PRELOAD_TARGET_SERVER
#define INIOVERRIDES_SAVED_DIR   "<server>\\StarRupture\\Saved\\Config\\WindowsServer\\"
#else
#error "Build a Client or Server configuration -- a preload plugin targets one executable."
#endif

static PreloadInfo s_info = {
    PRELOAD_INTERFACE_VERSION,
    "IniOverrides",
    "1.0.0",
    "AlienX",
    "Lets the game read Saved\\Config INI files (Engine.ini, Game.ini, ...) on top of its baked config",
    INIOVERRIDES_TARGET,
    100
};

// ---------------------------------------------------------------------------
// The patch site
// ---------------------------------------------------------------------------
//
// Anchored on the GameUserSettings compare result through the NoSave OR, so
// both halves of the if/else have to be there for it to match. The one
// wildcard is the NoSave field's offset inside FConfigBranch, which is 0x4A0 in
// the client and 0x4C0 in the dedicated server; everything else is identical
// in both, and the pattern is unique in both.
//
//   +0x00  85 C0                   test eax, eax
//   +0x02  75 09                   jnz  short +9          ; not GameUserSettings
//   +0x04  C6 87 81 01 00 00 01    mov  byte [rdi+181h], 1
//   +0x0B  EB 12                   jmp  short +0x12
//   +0x0D  48 8B 47 10             mov  rax, [rdi+10h]    ; Branch
//   +0x11  44 88 B7 81 01 00 00    mov  [rdi+181h], r14b  ; <-- patched
//   +0x18  80 88 ?? 04 00 00 02    or   byte [rax+4?0h], 2 ; NoSave, kept

static const char* const kHookName = "FConfigContext::PerformLoad (bAllowGeneratedIniWhenCooked)";

static const char* const kPattern =
    "85 C0 75 09 C6 87 81 01 00 00 01 EB 12 48 8B 47 10 "
    "44 88 B7 81 01 00 00 80 88 ?? 04 00 00 02";

static const int32_t kPatchOffset = 0x11;

static const uint8_t kOriginalBytes[] = { 0x44, 0x88, 0xB7, 0x81, 0x01, 0x00, 0x00 }; // mov [rdi+181h], r14b
static const uint8_t kPatchedBytes[]  = { 0xC6, 0x87, 0x81, 0x01, 0x00, 0x00, 0x01 }; // mov byte ptr [rdi+181h], 1

static_assert(sizeof(kOriginalBytes) == sizeof(kPatchedBytes), "patch must not change instruction length");

static uintptr_t s_patchAddress = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void FormatBytes(const uint8_t* bytes, size_t count, char* out, size_t outSize)
{
    static const char kHex[] = "0123456789ABCDEF";
    size_t pos = 0;
    for (size_t i = 0; i < count && pos + 3 < outSize; ++i)
    {
        if (i != 0)
            out[pos++] = ' ';
        out[pos++] = kHex[bytes[i] >> 4];
        out[pos++] = kHex[bytes[i] & 0x0F];
    }
    out[pos] = '\0';
}

// ---------------------------------------------------------------------------
// Exports
// ---------------------------------------------------------------------------

extern "C" __declspec(dllexport) PreloadInfo* GetPreloadInfo()
{
    return &s_info;
}

// Resolve only. A miss, a second match or an address outside a function
// refuses the plugin and the game boots with its stock behaviour -- which is
// the right outcome after a game update moves this code.
extern "C" __declspec(dllexport)
void PreloadScan(IPluginSelf* self, IPluginHookScanner* scanner)
{
    PluginScanRequest request = PLUGIN_SCAN_REQUEST_INIT;
    request.hookName     = kHookName;
    request.pattern      = kPattern;
    request.kind         = PLUGIN_SCAN_IN_FUNCTION;
    request.resultOffset = kPatchOffset;

    s_patchAddress = scanner->Resolve(self, &request);
}

extern "C" __declspec(dllexport)
bool PreloadInit(IPluginSelf* self, IPreloadPatch* patch)
{
    const uintptr_t base = patch->GetGameModuleBase();
    const uintptr_t rva  = s_patchAddress - base;

    self->logger->Info(self, "game build %s, loader %s",
                       patch->GetGameVersion(), patch->GetLoaderBuildTag());

    uint8_t current[sizeof(kOriginalBytes)] = {};
    if (!patch->ReadBytes(s_patchAddress, current, sizeof(current)))
    {
        self->logger->Error(self, "could not read the patch site at exe+0x%llX -- not patching",
                            static_cast<unsigned long long>(rva));
        return false;
    }

    char currentText[64];
    FormatBytes(current, sizeof(current), currentText, sizeof(currentText));

    if (std::memcmp(current, kPatchedBytes, sizeof(kPatchedBytes)) == 0)
    {
        self->logger->Info(self, "exe+0x%llX is already patched (%s) -- nothing to do",
                           static_cast<unsigned long long>(rva), currentText);
        return true;
    }

    // The pattern already covers these bytes, so this only trips if something
    // wrote to them between the scan and now. Refuse rather than overwrite
    // whatever that was.
    if (std::memcmp(current, kOriginalBytes, sizeof(kOriginalBytes)) != 0)
    {
        self->logger->Error(self, "unexpected bytes at exe+0x%llX (%s) -- something else has modified "
                                  "FConfigContext::PerformLoad, not patching",
                            static_cast<unsigned long long>(rva), currentText);
        return false;
    }

    if (!patch->WriteBytes(s_patchAddress, kPatchedBytes, sizeof(kPatchedBytes)))
    {
        self->logger->Error(self, "write to exe+0x%llX failed -- not patched",
                            static_cast<unsigned long long>(rva));
        return false;
    }

    uint8_t verify[sizeof(kPatchedBytes)] = {};
    if (!patch->ReadBytes(s_patchAddress, verify, sizeof(verify)) ||
        std::memcmp(verify, kPatchedBytes, sizeof(kPatchedBytes)) != 0)
    {
        // Put the original back rather than leave a half-written instruction
        // in a function the engine is about to run.
        patch->WriteBytes(s_patchAddress, kOriginalBytes, sizeof(kOriginalBytes));
        self->logger->Error(self, "patch at exe+0x%llX did not verify -- original bytes restored",
                            static_cast<unsigned long long>(rva));
        return false;
    }

    char patchedText[64];
    FormatBytes(kPatchedBytes, sizeof(kPatchedBytes), patchedText, sizeof(patchedText));

    self->logger->Info(self, "patched FConfigContext::PerformLoad at exe+0x%llX: %s -> %s",
                       static_cast<unsigned long long>(rva), currentText, patchedText);
    self->logger->Info(self, "user INI files in " INIOVERRIDES_SAVED_DIR
                             " (Engine.ini, Game.ini, Input.ini, Scalability.ini, ...) will now be "
                             "applied on top of the baked config; the game still does not write them");
    return true;
}

// The patch is left in place. It holds no pointer into this DLL, so it stays
// valid after the module is unloaded, and the process is going away anyway.
extern "C" __declspec(dllexport)
void PreloadShutdown()
{
}
