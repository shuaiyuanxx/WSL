/*++

Copyright (c) Microsoft. All rights reserved.

Module Name:

    DebugTransport.h

Abstract:

    Internal implementation for authenticated WSLC process debug transports.

--*/

#pragma once

#include <windows.h>
#include <memory>
#include <string>
#include <thread>
#include <wil/resource.h>

struct WslcDebugTransportImpl
{
    WslcDebugTransportImpl(
        wil::unique_handle&& standardInput,
        wil::unique_handle&& standardOutput,
        wil::unique_handle&& standardError,
        wil::unique_handle&& processExitEvent,
        PCWSTR pipeName,
        PCWSTR capabilityToken,
        PCWSTR correlationId,
        PCWSTR providerId);

    ~WslcDebugTransportImpl();

    WslcDebugTransportImpl(const WslcDebugTransportImpl&) = delete;
    WslcDebugTransportImpl& operator=(const WslcDebugTransportImpl&) = delete;

    wil::unique_event stopEvent{wil::EventOptions::ManualReset};
    std::thread worker;
};
