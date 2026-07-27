/*++

Copyright (c) Microsoft. All rights reserved.

Module Name:

    WSLCDebugPolicy.h

Abstract:

    Owned, immutable copy of the narrow debug policy propagated from the service
    into the per-user session process via WSLCSessionInitSettings::DebugPolicy.

    The service-owned WSLCDebugPolicy pointee is only valid for the duration of
    the synchronous CreateSession/Initialize marshaling, so the session copies the
    fields it needs into this owned structure during WSLCSession::Initialize and
    keeps it alive for the lifetime of the session. The capability token is a
    secret (256-bit) value and must never be logged or emitted in telemetry.

--*/

#pragma once

#include <array>
#include <optional>
#include <string>
#include <wil/resource.h>
#include "wslc.h"

namespace wsl::windows::service::wslc {

// Owned copy of the service debug policy. Scoped to the gdb-mi provider for the
// Preview. All strings are owned; the token is copied into a fixed buffer and
// zeroed on destruction.
struct WSLCDebugPolicyOwned
{
    WSLCDebugPolicyOwned() = default;
    WSLCDebugPolicyOwned(const WSLCDebugPolicyOwned&) = delete;
    WSLCDebugPolicyOwned& operator=(const WSLCDebugPolicyOwned&) = delete;
    WSLCDebugPolicyOwned(WSLCDebugPolicyOwned&&) = default;
    WSLCDebugPolicyOwned& operator=(WSLCDebugPolicyOwned&&) = default;

    ~WSLCDebugPolicyOwned()
    {
        SecureZeroMemory(Token.data(), Token.size());
    }

    GUID IntentId{};
    std::string ProviderId;

    std::string TargetProgram;
    std::string TargetWorkingDirectory;
    std::string DebuggerPath;

    std::string ImageReference;
    std::string ImageSha256Hex;

    std::wstring Endpoint;

    // Server-generated correlation/authorization token, in the form the debug
    // transport expects: 64 lowercase hex characters (the 32-byte token).
    std::wstring CapabilityTokenHex;

    // Correlation identifier the transport handshake validates. For the demo we
    // use the intent GUID string; it is not secret.
    std::wstring CorrelationId;

    std::array<byte, WSLC_DEBUG_INTENT_TOKEN_BYTES> Token{};

    // True once a container has consumed this policy so it is applied at most once.
    bool Consumed{false};
};

// Owns the backing storage for the strings/pointer arrays referenced by a debug-transformed
// WSLCContainerOptions. Must outlive the transformed options (i.e. the WSLCContainerImpl::Create
// call that consumes them).
struct DebugOptionTransformStorage
{
    std::string debuggerPath;
    std::string workingDirectory;
    std::string interpreterArg{"--interpreter=mi"};

    // Entrypoint = [ debuggerPath ]; Cmd = [ "--interpreter=mi" ].
    std::vector<LPCSTR> entrypointValues;
    std::vector<LPCSTR> commandLineValues;
};

// Applies the gdb-mi debug transform to a LOCAL copy of the container options without touching
// caller memory. Overrides the image entrypoint to the guest debugger in MI mode, sets the
// working directory, forces stdin and clears any TTY so the debug transport can relay stdio.
// The concrete cppdbg client sends `-file-exec-and-symbols` / launch, so gdb is NOT given a
// launch target here. Backing storage is owned by `storage` and must outlive `options`.
inline void ApplyDebugTransform(const WSLCDebugPolicyOwned& policy, WSLCContainerOptions& options, DebugOptionTransformStorage& storage)
{
    storage.debuggerPath = !policy.DebuggerPath.empty() ? policy.DebuggerPath : "/usr/bin/gdb";
    storage.workingDirectory = policy.TargetWorkingDirectory;

    // Entrypoint array: the debugger binary. Using Entrypoint (not Cmd) reliably overrides the
    // image's own entrypoint per Docker semantics.
    storage.entrypointValues = {storage.debuggerPath.c_str()};
    options.Entrypoint.Values = storage.entrypointValues.data();
    options.Entrypoint.Count = static_cast<ULONG>(storage.entrypointValues.size());

    // Cmd array (InitProcessOptions.CommandLine): the MI interpreter flag.
    storage.commandLineValues = {storage.interpreterArg.c_str()};
    options.InitProcessOptions.CommandLine.Values = storage.commandLineValues.data();
    options.InitProcessOptions.CommandLine.Count = static_cast<ULONG>(storage.commandLineValues.size());

    if (!storage.workingDirectory.empty())
    {
        options.InitProcessOptions.CurrentDirectory = storage.workingDirectory.c_str();
    }

    // Force stdin so the transport can drive gdb; ensure no TTY (gdb-mi is a byte protocol).
    options.InitProcessOptions.Flags =
        static_cast<WSLCProcessFlags>((options.InitProcessOptions.Flags | WSLCProcessFlagsStdin) & ~WSLCProcessFlagsTty);
}

// Copies the fixed-size 256-bit token to lowercase hex (64 chars). Never logs.
inline std::wstring DebugTokenToHex(const byte (&token)[WSLC_DEBUG_INTENT_TOKEN_BYTES])
{
    static constexpr wchar_t c_hex[] = L"0123456789abcdef";
    std::wstring hex;
    hex.reserve(WSLC_DEBUG_INTENT_TOKEN_BYTES * 2);
    for (const auto value : token)
    {
        hex.push_back(c_hex[(value >> 4) & 0xF]);
        hex.push_back(c_hex[value & 0xF]);
    }

    return hex;
}

} // namespace wsl::windows::service::wslc
