/*++

Copyright (c) Microsoft. All rights reserved.

Module Name:

    DebugTransport.cpp

Abstract:

    This file contains the implementation of the WinRT wrapper for a WSLC debug transport.

--*/

#include "precomp.h"
#include "DebugTransport.h"
#include "Microsoft.WSL.Containers.DebugTransport.g.cpp"

namespace winrt::Microsoft::WSL::Containers::implementation {

DebugTransport::DebugTransport(WslcDebugTransport transport) : m_transport(transport)
{
}

void DebugTransport::Close()
{
    m_transport.reset();
}

void DebugTransport::final_release(std::unique_ptr<DebugTransport> self)
{
    self->Close();
}

} // namespace winrt::Microsoft::WSL::Containers::implementation
