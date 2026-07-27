/*++

Copyright (c) Microsoft. All rights reserved.

Module Name:

    WSLCDebugIntentManager.cpp

Abstract:

    Implementation of the SYSTEM-service Debug Intent control plane. See
    WSLCDebugIntentManager.h for the design and invariants.

--*/

#include "WSLCDebugIntentManager.h"

#include <bcrypt.h>
#include <algorithm>
#include <filesystem>

#include "wslutil.h"
#include "WslSecurity.h"

namespace wslutil = wsl::windows::common::wslutil;

namespace {

// Allowlisted debug providers. Kept intentionally tiny for the gdb-mi Preview.
constexpr std::array<std::string_view, 1> c_allowedProviders = {"gdb-mi"};

// Upper bound on how long an intent may remain claimable. Prevents a caller from
// parking a reservation indefinitely.
constexpr ULONG c_maxIntentExpiryMs = 5 * 60 * 1000; // 5 minutes.
constexpr ULONG c_defaultIntentExpiryMs = 60 * 1000;  // 1 minute.

bool IsAllowedProvider(std::string_view Provider) noexcept
{
    return std::ranges::find(c_allowedProviders, Provider) != c_allowedProviders.end();
}

// Validates a NUL-terminated Linux absolute path with a length bound. Empty is
// rejected. Must start with '/'.
void ValidateLinuxAbsolutePath(LPCSTR Path, size_t MaxLength, const char* Field)
{
    THROW_HR_IF_MSG(E_INVALIDARG, Path == nullptr, "%hs is required", Field);
    const size_t length = strnlen(Path, MaxLength + 1);
    THROW_HR_IF_MSG(E_INVALIDARG, length == 0 || length > MaxLength, "%hs has invalid length", Field);
    THROW_HR_IF_MSG(E_INVALIDARG, Path[0] != '/', "%hs must be a Linux absolute path", Field);
}

// Validates a bounded, non-empty NUL-terminated narrow string.
std::string ValidateBoundedString(LPCSTR Value, size_t MaxLength, const char* Field)
{
    THROW_HR_IF_MSG(E_INVALIDARG, Value == nullptr, "%hs is required", Field);
    const size_t length = strnlen(Value, MaxLength + 1);
    THROW_HR_IF_MSG(E_INVALIDARG, length == 0 || length > MaxLength, "%hs has invalid length", Field);
    return std::string(Value, length);
}

// Validates an optional bounded string; returns nullopt when absent.
std::optional<std::string> ValidateOptionalString(LPCSTR Value, size_t MaxLength, const char* Field)
{
    if (Value == nullptr)
    {
        return std::nullopt;
    }
    return ValidateBoundedString(Value, MaxLength, Field);
}

// Validates a 64-char hex SHA-256, when present, and normalizes it to the
// canonical lowercase form used by the internal debug policy. Manifests accept
// either hex case, so the COM boundary must preserve that contract.
std::optional<std::string> ValidateOptionalSha256(LPCSTR Value)
{
    if (Value == nullptr)
    {
        return std::nullopt;
    }

    const size_t length = strnlen(Value, WSLC_DEBUG_SHA256_HEX_LENGTH + 1);
    THROW_HR_IF_MSG(E_INVALIDARG, length != WSLC_DEBUG_SHA256_HEX_LENGTH, "ImageSha256Hex must be 64 hex chars");

    std::string normalized(Value, length);
    for (char& c : normalized)
    {
        const bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        THROW_HR_IF_MSG(E_INVALIDARG, !isHex, "ImageSha256Hex must be hex");
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }

    return normalized;
}

std::wstring ToLower(std::wstring Value)
{
    std::ranges::transform(Value, Value.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return Value;
}

} // namespace

namespace wsl::windows::service::wslc {

DebugPolicyStorage::~DebugPolicyStorage() noexcept
{
    ClearToken();
}

DebugPolicyStorage::DebugPolicyStorage(DebugPolicyStorage&& Other) noexcept :
    Version(Other.Version),
    IntentId(Other.IntentId),
    ProviderId(std::move(Other.ProviderId)),
    TargetProgram(std::move(Other.TargetProgram)),
    TargetWorkingDirectory(std::move(Other.TargetWorkingDirectory)),
    DebuggerPath(std::move(Other.DebuggerPath)),
    ImageReference(std::move(Other.ImageReference)),
    ImageSha256Hex(std::move(Other.ImageSha256Hex)),
    Endpoint(std::move(Other.Endpoint))
{
    memcpy(Token.data(), Other.Token.data(), Token.size());
    Other.ClearToken();
}

DebugPolicyStorage& DebugPolicyStorage::operator=(DebugPolicyStorage&& Other) noexcept
{
    if (this != &Other)
    {
        ClearToken();
        Version = Other.Version;
        IntentId = Other.IntentId;
        ProviderId = std::move(Other.ProviderId);
        TargetProgram = std::move(Other.TargetProgram);
        TargetWorkingDirectory = std::move(Other.TargetWorkingDirectory);
        DebuggerPath = std::move(Other.DebuggerPath);
        ImageReference = std::move(Other.ImageReference);
        ImageSha256Hex = std::move(Other.ImageSha256Hex);
        Endpoint = std::move(Other.Endpoint);
        memcpy(Token.data(), Other.Token.data(), Token.size());
        Other.ClearToken();
    }

    return *this;
}

void DebugPolicyStorage::ClearToken() noexcept
{
    SecureZeroMemory(Token.data(), Token.size());
}

void DebugPolicyStorage::Fill(WSLCDebugPolicy& Policy) const noexcept
{
    Policy.Version = Version;
    Policy.IntentId = IntentId;
    Policy.ProviderId = ProviderId.c_str();
    Policy.TargetProgram = TargetProgram.c_str();
    Policy.TargetWorkingDirectory = TargetWorkingDirectory ? TargetWorkingDirectory->c_str() : nullptr;
    Policy.DebuggerPath = DebuggerPath.c_str();
    Policy.ImageReference = ImageReference ? ImageReference->c_str() : nullptr;
    Policy.ImageSha256Hex = ImageSha256Hex ? ImageSha256Hex->c_str() : nullptr;
    Policy.Endpoint = Endpoint.c_str();
    static_assert(sizeof(Policy.Token) == WSLC_DEBUG_INTENT_TOKEN_BYTES);
    memcpy(Policy.Token, Token.data(), WSLC_DEBUG_INTENT_TOKEN_BYTES);
}

bool DebugCallerIdentity::SameSid(const DebugCallerIdentity& Other) const noexcept
{
    return Sid.size() == Other.Sid.size() && Sid.size() > 0 && EqualSid(const_cast<PSID>(static_cast<const void*>(Sid.data())), const_cast<PSID>(static_cast<const void*>(Other.Sid.data())));
}

bool DebugCallerIdentity::SameProcess(const DebugCallerIdentity& Other) const noexcept
{
    return SameSid(Other) && Elevated == Other.Elevated && Pid == Other.Pid &&
           ProcessCreationTime.dwLowDateTime == Other.ProcessCreationTime.dwLowDateTime &&
           ProcessCreationTime.dwHighDateTime == Other.ProcessCreationTime.dwHighDateTime &&
           ImageFilePath == Other.ImageFilePath;
}

WSLCDebugIntentManagerImpl& WSLCDebugIntentManagerImpl::Instance()
{
    // Function-local static: constructed on first use, destroyed at process
    // exit. Intents are transient and process-scoped, so no cross-run state.
    static WSLCDebugIntentManagerImpl instance;
    return instance;
}

DebugCallerIdentity WSLCDebugIntentManagerImpl::CaptureRpcCallerIdentity()
{
    DebugCallerIdentity identity;

    // Impersonate the RPC caller to obtain its real SID and elevation.
    const auto token = wsl::windows::common::security::GetUserToken(TokenImpersonation);
    auto tokenUser = wil::get_token_information<TOKEN_USER>(token.get());
    const DWORD sidLen = GetLengthSid(tokenUser->User.Sid);
    identity.Sid.resize(sidLen);
    THROW_IF_WIN32_BOOL_FALSE(CopySid(sidLen, identity.Sid.data(), tokenUser->User.Sid));
    identity.Elevated =
        wil::test_token_membership(token.get(), SECURITY_NT_AUTHORITY, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS);

    // Open the authoritative calling process to bind PID + creation time + image
    // identity. Using the COM caller context avoids trusting anything the caller
    // sends in-band.
    const auto process = wslutil::OpenCallingProcess(PROCESS_QUERY_LIMITED_INFORMATION);
    if (process)
    {
        identity.Pid = GetProcessId(process.get());

        FILETIME creation{}, exit{}, kernel{}, user{};
        if (GetProcessTimes(process.get(), &creation, &exit, &kernel, &user))
        {
            identity.ProcessCreationTime = creation;
        }

        std::wstring imagePath;
        if (SUCCEEDED_LOG(wil::QueryFullProcessImageNameW<std::wstring>(process.get(), 0, imagePath)))
        {
            identity.ImageFilePath = ToLower(imagePath);
            identity.ImageFileName = ToLower(std::filesystem::path(imagePath).filename().wstring());
        }
    }

    return identity;
}

void WSLCDebugIntentManagerImpl::RegisterDebugIntent(_In_ const WSLCDebugIntentRequest* Request, _Out_ WSLCDebugIntentResult* Result)
{
    THROW_HR_IF_NULL(E_POINTER, Request);
    THROW_HR_IF_NULL(E_POINTER, Result);
    *Result = {};

    THROW_HR_IF_MSG(E_INVALIDARG, Request->Version != WSLC_DEBUG_INTENT_VERSION_1, "Unsupported intent version %lu", Request->Version);

    // Provider allowlist.
    const std::string providerId = ValidateBoundedString(Request->ProviderId, WSLC_DEBUG_MAX_PROVIDER_ID_LENGTH, "ProviderId");
    THROW_HR_IF_MSG(WSLC_E_DEBUG_PROVIDER_NOT_ALLOWED, !IsAllowedProvider(providerId), "Provider '%hs' is not allowed", providerId.c_str());

    // Expected host identity.
    THROW_HR_IF_MSG(E_INVALIDARG, Request->ExpectedHostFileName == nullptr || Request->ExpectedHostFileName[0] == L'\0', "ExpectedHostFileName is required");
    THROW_HR_IF_MSG(E_INVALIDARG, wcsnlen(Request->ExpectedHostFileName, WSLC_DEBUG_MAX_PATH_LENGTH + 1) > WSLC_DEBUG_MAX_PATH_LENGTH, "ExpectedHostFileName too long");
    std::wstring expectedHostFileName = ToLower(Request->ExpectedHostFileName);

    std::wstring expectedHostFilePath;
    if (Request->ExpectedHostFilePath != nullptr && Request->ExpectedHostFilePath[0] != L'\0')
    {
        THROW_HR_IF_MSG(E_INVALIDARG, wcsnlen(Request->ExpectedHostFilePath, WSLC_DEBUG_MAX_PATH_LENGTH + 1) > WSLC_DEBUG_MAX_PATH_LENGTH, "ExpectedHostFilePath too long");
        expectedHostFilePath = ToLower(Request->ExpectedHostFilePath);
    }

    // Linux target validation.
    ValidateLinuxAbsolutePath(Request->TargetProgram, WSLC_DEBUG_MAX_PATH_LENGTH, "TargetProgram");
    ValidateLinuxAbsolutePath(Request->DebuggerPath, WSLC_DEBUG_MAX_PATH_LENGTH, "DebuggerPath");
    std::optional<std::string> targetCwd;
    if (Request->TargetWorkingDirectory != nullptr)
    {
        ValidateLinuxAbsolutePath(Request->TargetWorkingDirectory, WSLC_DEBUG_MAX_PATH_LENGTH, "TargetWorkingDirectory");
        targetCwd = std::string(Request->TargetWorkingDirectory);
    }

    auto imageReference = ValidateOptionalString(Request->ImageReference, WSLC_DEBUG_MAX_IMAGE_REF_LENGTH, "ImageReference");
    auto imageSha256 = ValidateOptionalSha256(Request->ImageSha256Hex);

    // Clamp expiry.
    ULONG expiryMs = Request->ExpiryMs == 0 ? c_defaultIntentExpiryMs : Request->ExpiryMs;
    expiryMs = std::min(expiryMs, c_maxIntentExpiryMs);

    // Capture and bind the registering caller's identity.
    DebugIntent intent;
    intent.Owner = CaptureRpcCallerIdentity();
    THROW_HR_IF_MSG(E_ACCESSDENIED, intent.Owner.Sid.empty(), "Unable to capture caller SID");

    // Service-generated intent id.
    THROW_IF_FAILED(CoCreateGuid(&intent.Id));

    // Service-generated 256-bit token.
    THROW_IF_NTSTATUS_FAILED(
        BCryptGenRandom(nullptr, intent.Policy.Token.data(), static_cast<ULONG>(intent.Policy.Token.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG));

    // Service-generated named-pipe endpoint. The GUID keeps it unguessable and
    // unique; the token above authorizes the actual connection.
    const auto endpoint =
        std::format(L"\\\\.\\pipe\\wslc-debug-{}", wsl::shared::string::GuidToString<wchar_t>(intent.Id, wsl::shared::string::GuidToStringFlags::None));
    THROW_HR_IF(E_UNEXPECTED, endpoint.size() >= WSLC_DEBUG_ENDPOINT_MAX_LENGTH);

    // Build the immutable policy storage.
    intent.Policy.Version = WSLC_DEBUG_INTENT_VERSION_1;
    intent.Policy.IntentId = intent.Id;
    intent.Policy.ProviderId = providerId;
    intent.Policy.TargetProgram = Request->TargetProgram;
    intent.Policy.TargetWorkingDirectory = targetCwd;
    intent.Policy.DebuggerPath = Request->DebuggerPath;
    intent.Policy.ImageReference = imageReference;
    intent.Policy.ImageSha256Hex = imageSha256;
    intent.Policy.Endpoint = endpoint;

    intent.ExpectedHostFileName = expectedHostFileName;
    intent.ExpectedHostFilePath = expectedHostFilePath;

    // Expiry: track both a steady_clock deadline (for claim/expiry decisions,
    // immune to wall-clock changes) and a FILETIME the caller can compare.
    const auto now = std::chrono::steady_clock::now();
    intent.Expiry = now + std::chrono::milliseconds(expiryMs);

    FILETIME systemNow{};
    GetSystemTimeAsFileTime(&systemNow);
    ULARGE_INTEGER expiryTicks{};
    expiryTicks.LowPart = systemNow.dwLowDateTime;
    expiryTicks.HighPart = systemNow.dwHighDateTime;
    expiryTicks.QuadPart += static_cast<ULONGLONG>(expiryMs) * 10000ull; // ms -> 100ns.
    intent.ExpiryFileTime.dwLowDateTime = expiryTicks.LowPart;
    intent.ExpiryFileTime.dwHighDateTime = expiryTicks.HighPart;

    // Publish the intent before exposing its capability to the caller. If vector
    // insertion throws, DebugPolicyStorage's destructor securely clears the
    // generated token and Result remains zeroed. Once published, the output token
    // is copied from the registry entry and ownership transfers across COM.
    {
        std::lock_guard lock(m_lock);
        PurgeExpired();
        m_intents.push_back(std::move(intent));
        const auto& published = m_intents.back();

        Result->Version = WSLC_DEBUG_INTENT_VERSION_1;
        Result->IntentId = published.Id;
        wcsncpy_s(Result->Endpoint, std::size(Result->Endpoint), published.Policy.Endpoint.c_str(), _TRUNCATE);
        memcpy(Result->Token, published.Policy.Token.data(), WSLC_DEBUG_INTENT_TOKEN_BYTES);
        Result->ExpiryFileTime = expiryTicks.QuadPart;
    }

    // N.B. The capability token is NEVER logged.
    WSL_LOG(
        "WSLCRegisterDebugIntent",
        TraceLoggingValue(providerId.c_str(), "ProviderId"),
        TraceLoggingValue(expectedHostFileName.c_str(), "ExpectedHost"),
        TraceLoggingValue(expiryMs, "ExpiryMs"));
}

void WSLCDebugIntentManagerImpl::CancelDebugIntent(_In_ REFGUID IntentId)
{
    const auto caller = CaptureRpcCallerIdentity();

    std::lock_guard lock(m_lock);
    PurgeExpired();

    auto it = std::ranges::find_if(m_intents, [&](const DebugIntent& i) { return IsEqualGUID(i.Id, IntentId); });
    if (it == m_intents.end())
    {
        // Idempotent: already gone (cancelled/expired/claimed-and-removed).
        return;
    }

    // Ownership enforcement: only the registering principal may cancel.
    THROW_HR_IF(E_ACCESSDENIED, !it->Owner.SameProcess(caller));

    m_intents.erase(it);
}

void WSLCDebugIntentManagerImpl::GetDebugIntentState(_In_ REFGUID IntentId, _Out_ WSLCDebugIntentState* State)
{
    THROW_HR_IF_NULL(E_POINTER, State);

    const auto caller = CaptureRpcCallerIdentity();

    std::lock_guard lock(m_lock);

    auto it = std::ranges::find_if(m_intents, [&](const DebugIntent& i) { return IsEqualGUID(i.Id, IntentId); });
    THROW_HR_IF(WSLC_E_DEBUG_INTENT_NOT_FOUND, it == m_intents.end());

    THROW_HR_IF(E_ACCESSDENIED, !it->Owner.SameProcess(caller));

    // Expiry is terminal. Report it for this watch call, then immediately erase
    // the entry so its capability token is securely scrubbed rather than retained
    // until another registry operation.
    if (it->State == WSLCDebugIntentStatePending && std::chrono::steady_clock::now() >= it->Expiry)
    {
        *State = WSLCDebugIntentStateExpired;
        m_intents.erase(it);
        return;
    }

    *State = it->State;
}

std::vector<DebugIntent>::iterator WSLCDebugIntentManagerImpl::FindOwnedLockHeld(REFGUID IntentId, const DebugCallerIdentity& Caller)
{
    auto it = std::ranges::find_if(m_intents, [&](const DebugIntent& i) { return IsEqualGUID(i.Id, IntentId); });
    THROW_HR_IF(WSLC_E_DEBUG_INTENT_NOT_FOUND, it == m_intents.end());
    THROW_HR_IF(E_ACCESSDENIED, !it->Owner.SameProcess(Caller));
    return it;
}

std::optional<DebugPolicyStorage> WSLCDebugIntentManagerImpl::TryClaimForCaller(const DebugCallerIdentity& Caller)
{
    std::lock_guard lock(m_lock);
    PurgeExpired();

    const auto now = std::chrono::steady_clock::now();

    // Match: claimable + same SID/elevation context + expected host identity.
    // CreateSession runs in a different process from the registrant, so we
    // deliberately do not match PID/creation-time, but the integrity boundary
    // mirrors CheckTokenAccess: elevated and non-elevated contexts never cross.
    auto matches = [&](const DebugIntent& intent) {
        if (!intent.IsClaimable(now) || !intent.Owner.SameSid(Caller) || intent.Owner.Elevated != Caller.Elevated)
        {
            return false;
        }

        // The registrant declares which host binary will call CreateSession.
        // Match the authoritative caller's image name (required) and, when the
        // registrant pinned a full path, the full path too.
        if (Caller.ImageFileName.empty() || intent.ExpectedHostFileName != Caller.ImageFileName)
        {
            return false;
        }

        if (!intent.ExpectedHostFilePath.empty() && intent.ExpectedHostFilePath != Caller.ImageFilePath)
        {
            return false;
        }

        return true;
    };

    // Fail closed on ambiguity: more than one matching intent is an error rather
    // than a guess.
    DebugIntent* claimed = nullptr;
    for (auto& intent : m_intents)
    {
        if (matches(intent))
        {
            THROW_HR_IF(WSLC_E_DEBUG_INTENT_AMBIGUOUS, claimed != nullptr);
            claimed = &intent;
        }
    }

    if (claimed == nullptr)
    {
        return std::nullopt; // No match: ordinary session path.
    }

    // One-shot burn semantics: move the capability out and erase the registry
    // entry before session creation begins. It is never restored if the factory
    // or plugin later fails, so a failed attempt cannot replay the capability.
    // Move operations securely clear their source token; erase/destruction then
    // scrubs any remaining service-owned storage.
    claimed->State = WSLCDebugIntentStateClaimed;
    DebugPolicyStorage policy = std::move(claimed->Policy);

    auto id = claimed->Id;
    auto remove = std::ranges::remove_if(m_intents, [&](const DebugIntent& i) { return IsEqualGUID(i.Id, id); });
    m_intents.erase(remove.begin(), remove.end());

    return policy;
}

void WSLCDebugIntentManagerImpl::PurgeExpired() noexcept
{
    // Caller holds m_lock.
    const auto now = std::chrono::steady_clock::now();
    auto remove = std::ranges::remove_if(m_intents, [&](const DebugIntent& i) {
        return i.State != WSLCDebugIntentStatePending || now >= i.Expiry;
    });
    m_intents.erase(remove.begin(), remove.end());
}

} // namespace wsl::windows::service::wslc
