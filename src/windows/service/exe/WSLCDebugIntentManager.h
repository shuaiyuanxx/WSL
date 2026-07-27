/*++

Copyright (c) Microsoft. All rights reserved.

Module Name:

    WSLCDebugIntentManager.h

Abstract:

    Definition for the SYSTEM-service Debug Intent control plane.

    The Debug Intent control plane is an additive, out-of-band reservation
    mechanism used by a Windows host debug broker to pre-register its intent to
    debug the first process created in a session it is about to spawn.

    Threat model / invariants:
    ------------------------------------------------------------------------
    - Intents are bound to the registering RPC caller's real identity (SID +
      PID + process creation time + image identity + elevation). Only that
      identity may cancel/watch the intent.
    - Intents are one-shot: once claimed by a matching CreateSession they can
      never be claimed again.
    - Intents expire. Expired/claimed/cancelled intents are never claimable.
    - The service generates the intent id, the named-pipe endpoint and the
      256-bit token. Callers never supply secrets.
    - Tokens are never written to logs or telemetry.

    The registry is process-wide and owned by a singleton instance; the WSLC
    session manager coclass exposes IWSLCDebugIntentManager on top of it, and
    WSLCSessionManagerImpl::CreateSession consults it to claim a matching
    intent. Cleanup happens on cancel, expiry, failed session creation and
    service destruction.

--*/

#pragma once

#include "wslc.h"
#include <array>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace wsl::windows::service::wslc {

// Immutable copy of the debug policy backing storage. Kept alive by the caller
// (SessionEntry / a stack local) for as long as the marshaled WSLCDebugPolicy
// pointer must remain valid.
struct DebugPolicyStorage
{
    ULONG Version = WSLC_DEBUG_INTENT_VERSION_1;
    GUID IntentId{};
    std::string ProviderId;
    std::string TargetProgram;
    std::optional<std::string> TargetWorkingDirectory;
    std::string DebuggerPath;
    std::optional<std::string> ImageReference;
    std::optional<std::string> ImageSha256Hex;
    std::wstring Endpoint;
    std::array<BYTE, WSLC_DEBUG_INTENT_TOKEN_BYTES> Token{};

    DebugPolicyStorage() = default;
    ~DebugPolicyStorage() noexcept;

    // Capability-bearing policy storage is move-only. Moving transfers the
    // token and securely clears the source, which prevents vector relocation,
    // erase and return-value paths from leaving stale token copies behind.
    DebugPolicyStorage(const DebugPolicyStorage&) = delete;
    DebugPolicyStorage& operator=(const DebugPolicyStorage&) = delete;
    DebugPolicyStorage(DebugPolicyStorage&& Other) noexcept;
    DebugPolicyStorage& operator=(DebugPolicyStorage&& Other) noexcept;

    // Fills the marshaling struct with pointers into this storage. The returned
    // struct is only valid while *this is alive.
    void Fill(WSLCDebugPolicy& Policy) const noexcept;

private:
    void ClearToken() noexcept;
};

// Identity captured from the registering RPC caller (and re-derived at claim
// time from the authoritative CreateSession caller). Used to enforce that
// register / cancel / claim are performed by the same principal.
struct DebugCallerIdentity
{
    std::vector<BYTE> Sid;              // Raw SID bytes.
    bool Elevated = false;
    DWORD Pid = 0;
    FILETIME ProcessCreationTime{};     // Distinguishes PID reuse.
    std::wstring ImageFileName;         // e.g. "wsl.exe" (lowercased).
    std::wstring ImageFilePath;         // Full path when available (lowercased).

    // SID-only equality (used for the claim match: CreateSession runs in a
    // different process than the registrant, so PID/creation-time cannot match).
    bool SameSid(const DebugCallerIdentity& Other) const noexcept;

    // Full-identity equality (used for cancel/watch ownership: same SID,
    // elevation context, PID, process creation time and executable path).
    bool SameProcess(const DebugCallerIdentity& Other) const noexcept;
};

// A single registered intent.
struct DebugIntent
{
    GUID Id{};
    DebugCallerIdentity Owner;

    // Expected identity of the host process that will call CreateSession.
    std::wstring ExpectedHostFileName; // lowercased, required.
    std::wstring ExpectedHostFilePath; // lowercased, optional.

    DebugPolicyStorage Policy;

    std::chrono::steady_clock::time_point Expiry;
    FILETIME ExpiryFileTime{};

    WSLCDebugIntentState State = WSLCDebugIntentStatePending;

    bool IsClaimable(std::chrono::steady_clock::time_point Now) const noexcept
    {
        return State == WSLCDebugIntentStatePending && Now < Expiry;
    }
};

class WSLCDebugIntentManagerImpl
{
public:
    WSLCDebugIntentManagerImpl() = default;

    void RegisterDebugIntent(_In_ const WSLCDebugIntentRequest* Request, _Out_ WSLCDebugIntentResult* Result);
    void CancelDebugIntent(_In_ REFGUID IntentId);
    void GetDebugIntentState(_In_ REFGUID IntentId, _Out_ WSLCDebugIntentState* State);

    // Claims at most one matching intent for a CreateSession caller. Matches on
    // same SID + expected host identity, unexpired and unclaimed. Fails closed
    // (throws WSLC_E_DEBUG_INTENT_AMBIGUOUS) if more than one intent matches.
    // Returns std::nullopt when there is no match (ordinary session path).
    // On success the intent is transitioned to Claimed and its immutable policy
    // storage is returned to the caller, which owns the backing lifetime.
    std::optional<DebugPolicyStorage> TryClaimForCaller(const DebugCallerIdentity& Caller);

    // Purges expired intents. Safe to call opportunistically.
    void PurgeExpired() noexcept;

    // Process-wide singleton used by the coclass and by CreateSession.
    static WSLCDebugIntentManagerImpl& Instance();

    // Captures the identity of the current RPC caller (SID, elevation, PID,
    // process creation time, image path). Used by RegisterDebugIntent.
    static DebugCallerIdentity CaptureRpcCallerIdentity();

private:
    // Finds an intent by id and validates the current RPC caller owns it.
    // Returns an iterator or throws WSLC_E_DEBUG_INTENT_NOT_FOUND.
    std::vector<DebugIntent>::iterator FindOwnedLockHeld(REFGUID IntentId, const DebugCallerIdentity& Caller);

    std::mutex m_lock;
    _Guarded_by_(m_lock) std::vector<DebugIntent> m_intents;
};

} // namespace wsl::windows::service::wslc
