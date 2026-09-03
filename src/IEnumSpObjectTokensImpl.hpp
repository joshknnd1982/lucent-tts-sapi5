#pragma once

#include <vector>
#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>
#include <comdef.h>
#include <comip.h>

#include "com.hpp"
#include "voice_attributes.hpp"
#include "voice_token.hpp"

namespace Lucent {
namespace sapi {

class __declspec(uuid("3b9d0c62-6f7e-4a1b-9e2d-5c8f1a7b3d40")) IEnumSpObjectTokensImpl :
    public IEnumSpObjectTokens
{
public:
    explicit IEnumSpObjectTokensImpl(bool initialize = true);

    IEnumSpObjectTokensImpl(const IEnumSpObjectTokensImpl&) = delete;
    IEnumSpObjectTokensImpl& operator=(const IEnumSpObjectTokensImpl&) = delete;

    STDMETHOD(Next)(ULONG celt, ISpObjectToken** pelt, ULONG* pceltFetched) override;
    STDMETHOD(Skip)(ULONG celt) override;
    STDMETHOD(Reset)() override;
    STDMETHOD(Clone)(IEnumSpObjectTokens** ppEnum) override;
    STDMETHOD(Item)(ULONG Index, ISpObjectToken** ppToken) override;
    STDMETHOD(GetCount)(ULONG* pulCount) override;

protected:
    [[nodiscard]] void* get_interface(REFIID riid) noexcept
    {
        return com::try_primary_interface<IEnumSpObjectTokens>(this, riid);
    }

private:
    _COM_SMARTPTR_TYPEDEF(ISpObjectToken, __uuidof(ISpObjectToken));
    _COM_SMARTPTR_TYPEDEF(ISpObjectTokenInit, __uuidof(ISpObjectTokenInit));

    [[nodiscard]] ISpObjectTokenPtr create_token(const voice_attributes& attr) const;

    std::size_t index_;
    std::vector<voice_attributes> sapi_voices_;
};
}
}
