#include <new>
#include <comdef.h>
#include "voice_token.hpp"
#include "ISpTTSEngineImpl.hpp"

namespace Lucent {
namespace sapi {

voice_token::voice_token(const voice_attributes& attr)
    : voice_index_(static_cast<DWORD>(attr.get_index()))
{
    const std::wstring name = attr.get_name();
    set(name);

    utils::out_ptr<wchar_t> clsid_str(CoTaskMemFree);
    StringFromCLSID(__uuidof(ISpTTSEngineImpl), clsid_str.address());
    set(L"CLSID", clsid_str.get());
    set(L"VoiceIndex", std::to_wstring(voice_index_));

    attributes_[L"Age"] = attr.get_age();
    attributes_[L"Vendor"] = L"Lucent";
    attributes_[L"Language"] = attr.get_language();
    attributes_[L"Gender"] = attr.get_gender();
    attributes_[L"Name"] = name;
}

STDMETHODIMP voice_token::GetDWORD(LPCWSTR pszKeyName, DWORD* pdwValue)
{
    if (!pdwValue) {
        return E_POINTER;
    }
    if (pszKeyName && _wcsicmp(pszKeyName, L"VoiceIndex") == 0) {
        *pdwValue = voice_index_;
        return S_OK;
    }
    return ISpDataKeyImpl::GetDWORD(pszKeyName, pdwValue);
}

STDMETHODIMP voice_token::OpenKey(LPCWSTR pszSubKeyName, ISpDataKey** ppSubKey)
{
    if (!pszSubKeyName) {
        return E_INVALIDARG;
    }
    if (!ppSubKey) {
        return E_POINTER;
    }
    *ppSubKey = nullptr;

    try {
        if (!str_equal(pszSubKeyName, L"Attributes")) {
            return SPERR_NOT_FOUND;
        }

        com::object<ISpDataKeyImpl> obj;
        for (const auto& [key, value] : attributes_) {
            obj->set(key, value);
        }

        com::interface_ptr<ISpDataKey> int_ptr(obj);
        *ppSubKey = int_ptr.get();
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

STDMETHODIMP voice_token::EnumKeys(ULONG Index, LPWSTR* ppszSubKeyName)
{
    if (!ppszSubKeyName) {
        return E_POINTER;
    }
    *ppszSubKeyName = nullptr;

    if (Index > 0) {
        return SPERR_NO_MORE_ITEMS;
    }

    try {
        *ppszSubKeyName = com::strdup(L"Attributes");
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}
}
}
