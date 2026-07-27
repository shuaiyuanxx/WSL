/*++

Copyright (c) Microsoft. All rights reserved.

Module Name:

    DebugTransport.h

Abstract:

    This file contains the definition of the WinRT wrapper for a WSLC debug transport.

--*/

#pragma once
#include "Microsoft.WSL.Containers.DebugTransport.g.h"
#include "Helpers.h"

namespace winrt::Microsoft::WSL::Containers::implementation {

struct DebugTransport : DebugTransportT<DebugTransport>
{
    DebugTransport() = default;
    explicit DebugTransport(WslcDebugTransport transport);

    void Close();
    static void final_release(std::unique_ptr<DebugTransport> self);

private:
    wil::unique_any<WslcDebugTransport, decltype(&WslcReleaseDebugTransport), &WslcReleaseDebugTransport> m_transport{nullptr};
};

} // namespace winrt::Microsoft::WSL::Containers::implementation
