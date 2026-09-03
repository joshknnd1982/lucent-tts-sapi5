#include <stdexcept>
#include <new>
#include <algorithm>

#include "IEnumSpObjectTokensImpl.hpp"
#include "lucent_log.h"

namespace Lucent {
namespace sapi {

IEnumSpObjectTokensImpl::IEnumSpObjectTokensImpl(bool initialize)
    : index_(0)
{
    if (!initialize) {
        return;
    }

    const int n = voice_count();
    sapi_voices_.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        sapi_voices_.emplace_back(i);
    }
    LLOG("enumerator: %d voices", n);
}

IEnumSpObjectTokensImpl::ISpObjectTokenPtr IEnumSpObjectTokensImpl::create_token(
    const voice_attributes& attr) const
{
    std::wstring token_id = std::wstring(SPCAT_VOICES) + L"\\TokenEnums\\LucentTTS\\" + attr.get_token_name();
    com::object<voice_token> obj_data_key(attr);
    com::interface_ptr<ISpDataKey> int_data_key(obj_data_key);

    ISpObjectTokenInitPtr int_token_init(CLSID_SpObjectToken);
    if (!int_token_init) {
        throw std::runtime_error("Unable to create an object token");
    }

    if (FAILED(int_token_init->InitFromDataKey(SPCAT_VOICES, token_id.c_str(), int_data_key.get(false)))) {
        throw std::runtime_error("Unable to initialize an object token");
    }

    ISpObjectTokenPtr int_token = int_token_init;
    return int_token;
}

STDMETHODIMP IEnumSpObjectTokensImpl::Next(ULONG celt, ISpObjectToken** pelt, ULONG* pceltFetched)
{
    if (celt == 0) {
        return E_INVALIDARG;
    }
    if (!pelt) {
        return E_POINTER;
    }
    if (!pceltFetched && celt > 1) {
        return E_POINTER;
    }

    if (pceltFetched) {
        *pceltFetched = 0;
    }

    try {
        std::vector<ISpObjectTokenPtr> tokens;
        tokens.reserve(celt);

        const std::size_t max_index = sapi_voices_.size();
        const std::size_t next_index = (std::min)(index_ + static_cast<std::size_t>(celt), max_index);

        for (std::size_t i = index_; i < next_index; ++i) {
            tokens.push_back(create_token(sapi_voices_[i]));
        }

        for (std::size_t i = 0; i < tokens.size(); ++i) {
            tokens[i].AddRef();
            pelt[i] = tokens[i].GetInterfacePtr();
        }

        if (pceltFetched) {
            *pceltFetched = static_cast<ULONG>(tokens.size());
        }

        index_ += tokens.size();
        return (tokens.size() == celt) ? S_OK : S_FALSE;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

STDMETHODIMP IEnumSpObjectTokensImpl::Skip(ULONG celt)
{
    const std::size_t remaining = sapi_voices_.size() - index_;
    const std::size_t num_skipped = (std::min)(remaining, static_cast<std::size_t>(celt));
    index_ += num_skipped;
    return (num_skipped == celt) ? S_OK : S_FALSE;
}

STDMETHODIMP IEnumSpObjectTokensImpl::Reset()
{
    index_ = 0;
    return S_OK;
}

STDMETHODIMP IEnumSpObjectTokensImpl::GetCount(ULONG* pulCount)
{
    if (!pulCount) {
        return E_POINTER;
    }
    *pulCount = static_cast<ULONG>(sapi_voices_.size());
    return S_OK;
}

STDMETHODIMP IEnumSpObjectTokensImpl::Item(ULONG Index, ISpObjectToken** ppToken)
{
    if (!ppToken) {
        return E_POINTER;
    }
    *ppToken = nullptr;

    if (Index >= sapi_voices_.size()) {
        return SPERR_NO_MORE_ITEMS;
    }

    try {
        ISpObjectTokenPtr int_token = create_token(sapi_voices_[Index]);
        int_token.AddRef();
        *ppToken = int_token.GetInterfacePtr();
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

STDMETHODIMP IEnumSpObjectTokensImpl::Clone(IEnumSpObjectTokens** ppEnum)
{
    if (!ppEnum) {
        return E_POINTER;
    }
    *ppEnum = nullptr;

    try {
        com::object<IEnumSpObjectTokensImpl> obj(false);
        obj->sapi_voices_ = sapi_voices_;
        obj->index_ = index_;
        com::interface_ptr<IEnumSpObjectTokens> int_ptr(obj);
        *ppEnum = int_ptr.get();
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
