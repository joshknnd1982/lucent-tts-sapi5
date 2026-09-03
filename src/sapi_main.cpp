#include <new>
#include <sapi.h>
#include "com.hpp"
#include "registry.hpp"
#include "ISpTTSEngineImpl.hpp"
#include "IEnumSpObjectTokensImpl.hpp"
#include "lucent_log.h"

namespace {

HINSTANCE g_dll_handle = nullptr;
Lucent::com::class_object_factory g_cls_obj_factory;

const std::wstring token_enums_path = L"Software\\Microsoft\\Speech\\Voices\\TokenEnums";

[[nodiscard]] std::wstring clsid_to_string(const GUID& clsid)
{
    wchar_t buf[64];
    StringFromGUID2(clsid, buf, 64);
    return std::wstring(buf);
}

void register_token_enumerator()
{
    using namespace Lucent::sapi;
    using namespace Lucent::registry;

    const std::wstring clsid_str = clsid_to_string(__uuidof(IEnumSpObjectTokensImpl));

    key enums_key(HKEY_LOCAL_MACHINE, token_enums_path, KEY_CREATE_SUB_KEY | KEY_SET_VALUE, true);
    key enum_key(enums_key, L"LucentTTS", KEY_SET_VALUE, true);

    enum_key.set(L"Lucent TTS Voices");
    enum_key.set(L"CLSID", clsid_str);
}

void unregister_token_enumerator() noexcept
{
    using namespace Lucent::registry;

    try {
        key enums_key(HKEY_LOCAL_MACHINE, token_enums_path, KEY_ALL_ACCESS);
        enums_key.delete_subkey(L"LucentTTS");
    }
    catch (...) {
    }
}
}

BOOL APIENTRY DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID /*lpReserved*/)
{
    if (dwReason == DLL_PROCESS_ATTACH) {
        g_dll_handle = hInstance;
        DisableThreadLibraryCalls(hInstance);

        try {
            Lucent::sapi::InitEngine(hInstance);
            g_cls_obj_factory.register_class<Lucent::sapi::IEnumSpObjectTokensImpl>();
            g_cls_obj_factory.register_class<Lucent::sapi::ISpTTSEngineImpl>();
        }
        catch (...) {
            return FALSE;
        }
    }
    else if (dwReason == DLL_PROCESS_DETACH) {
        Lucent::sapi::CleanupEngine();
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    return g_cls_obj_factory.create(rclsid, riid, ppv);
}

STDAPI DllCanUnloadNow()
{
    return Lucent::com::object_counter::is_zero() ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer()
{
    try {
        Lucent::com::class_registrar r(g_dll_handle);
        r.register_class<Lucent::sapi::IEnumSpObjectTokensImpl>();
        r.register_class<Lucent::sapi::ISpTTSEngineImpl>();
        register_token_enumerator();
        LLOG("registration: OK");
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (const Lucent::registry::error&) {
        // TokenEnums must live in HKLM; an unelevated regsvr32 cannot succeed.
        return E_ACCESSDENIED;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

STDAPI DllUnregisterServer()
{
    try {
        unregister_token_enumerator();
        Lucent::com::class_registrar r(g_dll_handle);
        r.unregister_class<Lucent::sapi::IEnumSpObjectTokensImpl>();
        r.unregister_class<Lucent::sapi::ISpTTSEngineImpl>();
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}
