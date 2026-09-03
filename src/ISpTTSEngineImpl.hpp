#pragma once

#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <comdef.h>
#include <comip.h>
#include "com.hpp"
#include "voice_attributes.hpp"
#include "lucent_engine.h"

namespace Lucent {
namespace sapi {

class __declspec(uuid("{8e4f2a91-5c3d-4b7e-a6f0-2d9c1b8e7a55}")) ISpTTSEngineImpl :
    public ISpTTSEngine, public ISpObjectWithToken
{
public:
    ISpTTSEngineImpl();
    ~ISpTTSEngineImpl();

    ISpTTSEngineImpl(const ISpTTSEngineImpl&) = delete;
    ISpTTSEngineImpl& operator=(const ISpTTSEngineImpl&) = delete;

    STDMETHOD(Speak)(DWORD dwSpeakFlags, REFGUID rguidFormatId,
                     const WAVEFORMATEX* pWaveFormatEx, const SPVTEXTFRAG* pTextFragList,
                     ISpTTSEngineSite* pOutputSite) override;
    STDMETHOD(GetOutputFormat)(const GUID* pTargetFmtId, const WAVEFORMATEX* pTargetWaveFormatEx,
                               GUID* pOutputFormatId, WAVEFORMATEX** ppCoMemOutputWaveFormatEx) override;

    STDMETHOD(SetObjectToken)(ISpObjectToken* pToken) override;
    STDMETHOD(GetObjectToken)(ISpObjectToken** ppToken) override;

protected:
    [[nodiscard]] void* get_interface(REFIID riid) noexcept
    {
        void* ptr = com::try_primary_interface<ISpTTSEngine>(this, riid);
        return ptr ? ptr : com::try_interface<ISpObjectWithToken>(this, riid);
    }

private:
    _COM_SMARTPTR_TYPEDEF(ISpObjectToken, __uuidof(ISpObjectToken));
    _COM_SMARTPTR_TYPEDEF(ISpDataKey, __uuidof(ISpDataKey));

    // Builds the engine request for this voice from the token, the settings file and the
    // SAPI prosody values.
    bool build_request(lucent::VoiceRequest& req, long sapi_rate, USHORT sapi_volume, int sapi_pitch);

    ISpObjectTokenPtr token_;
    voice_attributes voice_;
    int sample_rate_;
};

// Process-wide engine access.
void InitEngine(HINSTANCE dll);
void CleanupEngine();

}
}
