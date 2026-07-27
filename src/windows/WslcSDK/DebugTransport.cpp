/*++

Copyright (c) Microsoft. All rights reserved.

Module Name:

    DebugTransport.cpp

Abstract:

    Internal implementation for authenticated WSLC process debug transports.

--*/

#include "precomp.h"
#include "DebugTransport.h"
#include "relay.hpp"
#include <winrt/Windows.Data.Json.h>

namespace {

constexpr uint32_t c_protocolVersion = 1;
constexpr uint32_t c_maximumMessageBytes = 64 * 1024;
constexpr size_t c_relayBufferSize = 16 * 1024;

std::wstring NormalizePipeName(PCWSTR pipeName)
{
    THROW_HR_IF_NULL(E_POINTER, pipeName);
    THROW_HR_IF(E_INVALIDARG, *pipeName == L'\0');

    constexpr std::wstring_view pipePrefix = L"\\\\.\\pipe\\";
    if (std::wstring_view{pipeName}.starts_with(pipePrefix))
    {
        return pipeName;
    }

    THROW_HR_IF(E_INVALIDARG, wcschr(pipeName, L'\\') != nullptr || wcschr(pipeName, L'/') != nullptr);
    return std::format(L"{}{}", pipePrefix, pipeName);
}

void ValidateCapabilityToken(PCWSTR capabilityToken)
{
    THROW_HR_IF_NULL(E_POINTER, capabilityToken);
    constexpr size_t capabilityTokenLength = 64;
    THROW_HR_IF(E_INVALIDARG, wcslen(capabilityToken) != capabilityTokenLength);

    for (size_t index = 0; index < capabilityTokenLength; ++index)
    {
        const auto character = capabilityToken[index];
        const bool hexadecimal =
            (character >= L'0' && character <= L'9') ||
            (character >= L'a' && character <= L'f') ||
            (character >= L'A' && character <= L'F');
        THROW_HR_IF(E_INVALIDARG, !hexadecimal);
    }
}

wil::unique_hlocal CreatePipeSecurityDescriptor()
{
    const auto tokenUser = wil::get_token_information<TOKEN_USER>(GetCurrentProcessToken());
    wil::unique_hlocal_string userSid;
    THROW_LAST_ERROR_IF(!ConvertSidToStringSidW(tokenUser->User.Sid, &userSid));

    const auto sddl = std::format(L"D:P(A;;GA;;;SY)(A;;GA;;;{})", userSid.get());
    PSECURITY_DESCRIPTOR descriptor{};
    THROW_IF_WIN32_BOOL_FALSE(
        ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr));
    return wil::unique_hlocal{descriptor};
}

wil::unique_hfile CreateServerPipe(PCWSTR pipeName)
{
    auto securityDescriptor = CreatePipeSecurityDescriptor();
    SECURITY_ATTRIBUTES securityAttributes{sizeof(securityAttributes), securityDescriptor.get(), FALSE};

    wil::unique_hfile pipe{CreateNamedPipeW(
        pipeName,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1,
        LX_RELAY_BUFFER_SIZE,
        LX_RELAY_BUFFER_SIZE,
        0,
        &securityAttributes)};
    THROW_LAST_ERROR_IF(!pipe);
    return pipe;
}

bool ConnectClient(HANDLE pipe, HANDLE stopEvent, HANDLE processExitEvent)
{
    OVERLAPPED overlapped{};
    wil::unique_event connectedEvent{wil::EventOptions::ManualReset};
    overlapped.hEvent = connectedEvent.get();

    if (ConnectNamedPipe(pipe, &overlapped))
    {
        return true;
    }

    const auto error = GetLastError();
    if (error == ERROR_PIPE_CONNECTED)
    {
        return true;
    }

    THROW_LAST_ERROR_IF(error != ERROR_IO_PENDING);

    const HANDLE waits[]{connectedEvent.get(), stopEvent, processExitEvent};
    const auto waitResult = WaitForMultipleObjects(RTL_NUMBER_OF(waits), waits, FALSE, INFINITE);
    if (waitResult == WAIT_OBJECT_0)
    {
        DWORD bytesTransferred{};
        if (!GetOverlappedResult(pipe, &overlapped, &bytesTransferred, FALSE))
        {
            const auto connectError = GetLastError();
            THROW_HR_IF(HRESULT_FROM_WIN32(connectError), connectError != ERROR_PIPE_CONNECTED);
        }

        return true;
    }

    if (waitResult == WAIT_OBJECT_0 + 1 || waitResult == WAIT_OBJECT_0 + 2)
    {
        CancelIoEx(pipe, &overlapped);
        DWORD bytesTransferred{};
        GetOverlappedResult(pipe, &overlapped, &bytesTransferred, TRUE);
        return false;
    }

    THROW_LAST_ERROR_IF(waitResult == WAIT_FAILED);
    THROW_HR(E_UNEXPECTED);
}

bool ClientMatchesCurrentUser(HANDLE pipe)
{
    ULONG clientProcessId{};
    if (!GetNamedPipeClientProcessId(pipe, &clientProcessId))
    {
        return false;
    }

    wil::unique_handle clientProcess{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, clientProcessId)};
    if (!clientProcess)
    {
        return false;
    }

    wil::unique_handle clientToken;
    if (!OpenProcessToken(clientProcess.get(), TOKEN_QUERY, &clientToken))
    {
        return false;
    }

    const auto clientUser = wil::get_token_information<TOKEN_USER>(clientToken.get());
    const auto serverUser = wil::get_token_information<TOKEN_USER>(GetCurrentProcessToken());
    return EqualSid(clientUser->User.Sid, serverUser->User.Sid) != FALSE;
}

std::string ReadFramedMessage(HANDLE pipe, const std::vector<HANDLE>& exitEvents)
{
    std::array<gsl::byte, sizeof(uint32_t)> lengthBytes{};
    size_t offset{};
    while (offset < lengthBytes.size())
    {
        const auto bytesRead = wsl::windows::common::relay::InterruptableRead(
            pipe,
            gsl::make_span(lengthBytes).subspan(offset),
            exitEvents);
        THROW_HR_IF(HRESULT_FROM_WIN32(ERROR_BROKEN_PIPE), bytesRead == 0);
        offset += bytesRead;
    }

    uint32_t payloadLength{};
    memcpy(&payloadLength, lengthBytes.data(), sizeof(payloadLength));
    THROW_HR_IF(E_INVALIDARG, payloadLength == 0 || payloadLength > c_maximumMessageBytes);

    std::string payload(payloadLength, '\0');
    offset = 0;
    while (offset < payload.size())
    {
        auto remaining = gsl::make_span(reinterpret_cast<gsl::byte*>(payload.data()), payload.size()).subspan(offset);
        const auto bytesRead = wsl::windows::common::relay::InterruptableRead(pipe, remaining, exitEvents);
        THROW_HR_IF(HRESULT_FROM_WIN32(ERROR_BROKEN_PIPE), bytesRead == 0);
        offset += bytesRead;
    }

    return payload;
}

void WriteAll(HANDLE handle, gsl::span<const gsl::byte> buffer, const std::vector<HANDLE>& exitEvents)
{
    size_t offset{};
    while (offset < buffer.size())
    {
        OVERLAPPED overlapped{};
        wil::unique_event event{wil::EventOptions::ManualReset};
        overlapped.hEvent = event.get();
        const auto bytesWritten = wsl::windows::common::relay::InterruptableWrite(
            handle,
            buffer.subspan(offset),
            exitEvents,
            &overlapped);
        THROW_HR_IF(HRESULT_FROM_WIN32(ERROR_BROKEN_PIPE), bytesWritten == 0);
        offset += bytesWritten;
    }
}

void WriteFramedMessage(HANDLE pipe, std::string_view payload, const std::vector<HANDLE>& exitEvents)
{
    const auto length = gsl::narrow_cast<uint32_t>(payload.size());
    WriteAll(pipe, gsl::make_span(reinterpret_cast<const gsl::byte*>(&length), sizeof(length)), exitEvents);
    WriteAll(pipe, gsl::make_span(reinterpret_cast<const gsl::byte*>(payload.data()), payload.size()), exitEvents);
}

bool FixedTimeEquals(std::string_view left, std::string_view right)
{
    size_t difference = left.size() ^ right.size();
    const auto maximumLength = std::max(left.size(), right.size());
    for (size_t index = 0; index < maximumLength; ++index)
    {
        const auto leftByte = index < left.size() ? static_cast<unsigned char>(left[index]) : 0;
        const auto rightByte = index < right.size() ? static_cast<unsigned char>(right[index]) : 0;
        difference |= leftByte ^ rightByte;
    }

    return difference == 0;
}

std::optional<std::string> ValidateHandshake(
    std::string_view payload,
    std::string_view expectedToken,
    std::string_view expectedCorrelationId,
    std::string_view expectedProviderId)
{
    using namespace winrt::Windows::Data::Json;

    const auto json = JsonObject::Parse(wsl::windows::common::string::MultiByteToWide(payload));
    const auto version = gsl::narrow_cast<uint32_t>(json.GetNamedNumber(L"protocolVersion"));
    if (version != c_protocolVersion)
    {
        return "Unsupported debug protocol version.";
    }

    const auto token = wsl::windows::common::string::WideToMultiByte(json.GetNamedString(L"token"));
    if (!FixedTimeEquals(token, expectedToken))
    {
        return "Invalid debug capability token.";
    }

    const auto correlationId = wsl::windows::common::string::WideToMultiByte(json.GetNamedString(L"correlationId"));
    if (correlationId != expectedCorrelationId)
    {
        return "Debug correlation ID does not match.";
    }

    const auto providerId = wsl::windows::common::string::WideToMultiByte(json.GetNamedString(L"providerId"));
    if (providerId != expectedProviderId)
    {
        return "Debug provider does not match.";
    }

    return std::nullopt;
}

std::string CreateHandshakeResult(const std::optional<std::string>& rejection)
{
    if (!rejection)
    {
        return std::format("{{\"protocolVersion\":{},\"accepted\":true,\"error\":null}}", c_protocolVersion);
    }

    return std::format(
        "{{\"protocolVersion\":{},\"accepted\":false,\"error\":\"{}\"}}",
        c_protocolVersion,
        *rejection);
}

void RelayDebugger(
    HANDLE pipe,
    HANDLE standardInput,
    HANDLE standardOutput,
    HANDLE standardError,
    HANDLE stopEvent,
    HANDLE processExitEvent)
{
    wil::unique_event relayStopEvent{wil::EventOptions::ManualReset};
    const HANDLE relayStopHandle = relayStopEvent.get();
    const std::vector<HANDLE> exitEvents{stopEvent, processExitEvent, relayStopHandle};

    std::thread inputRelay([pipe, standardInput, relayStopHandle]() {
        try
        {
            wsl::windows::common::relay::InterruptableRelay(pipe, standardInput, relayStopHandle, c_relayBufferSize);
        }
        CATCH_LOG();
        LOG_IF_WIN32_BOOL_FALSE(SetEvent(relayStopHandle));
    });

    std::thread errorDrain;
    if (standardError)
    {
        errorDrain = std::thread([standardError, relayStopHandle]() {
            try
            {
                wsl::windows::common::relay::InterruptableRelay(standardError, nullptr, relayStopHandle, c_relayBufferSize);
            }
            CATCH_LOG();
        });
    }

    auto stopRelays = wil::scope_exit_log(WI_DIAGNOSTICS_INFO, [&]() {
        relayStopEvent.SetEvent();
        CancelIoEx(pipe, nullptr);
        if (inputRelay.joinable())
        {
            inputRelay.join();
        }
        if (errorDrain.joinable())
        {
            errorDrain.join();
        }
    });

    std::array<gsl::byte, c_relayBufferSize> buffer{};
    for (;;)
    {
        const auto bytesRead = wsl::windows::common::relay::InterruptableRead(
            standardOutput,
            gsl::make_span(buffer),
            exitEvents);
        if (bytesRead == 0)
        {
            break;
        }

        WriteAll(pipe, gsl::make_span(buffer).first(bytesRead), exitEvents);
    }
}

void RunTransport(
    std::wstring pipeName,
    std::string token,
    std::string correlationId,
    std::string providerId,
    wil::unique_handle standardInput,
    wil::unique_handle standardOutput,
    wil::unique_handle standardError,
    wil::unique_handle processExitEvent,
    HANDLE stopEvent)
try
{
    auto clearToken = wil::scope_exit([&]() { SecureZeroMemory(token.data(), token.size()); });
    wsl::windows::common::wslutil::SetThreadDescription(L"WSLC Debug Transport");

    while (WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0 &&
           WaitForSingleObject(processExitEvent.get(), 0) != WAIT_OBJECT_0)
    {
        auto pipe = CreateServerPipe(pipeName.c_str());
        if (!ConnectClient(pipe.get(), stopEvent, processExitEvent.get()))
        {
            return;
        }

        try
        {
            if (!ClientMatchesCurrentUser(pipe.get()))
            {
                DisconnectNamedPipe(pipe.get());
                continue;
            }

            const std::vector<HANDLE> exitEvents{stopEvent, processExitEvent.get()};
            const auto handshake = ReadFramedMessage(pipe.get(), exitEvents);
            const auto rejection = ValidateHandshake(handshake, token, correlationId, providerId);
            const auto response = CreateHandshakeResult(rejection);
            WriteFramedMessage(pipe.get(), response, exitEvents);
            if (rejection)
            {
                // Ensure the client receives the rejection frame before the server disconnects.
                // DisconnectNamedPipe discards unread pipe data.
                FlushFileBuffers(pipe.get());
                DisconnectNamedPipe(pipe.get());
                continue;
            }

            RelayDebugger(
                pipe.get(),
                standardInput.get(),
                standardOutput.get(),
                standardError.get(),
                stopEvent,
                processExitEvent.get());
            return;
        }
        catch (...)
        {
            LOG_CAUGHT_EXCEPTION();
            DisconnectNamedPipe(pipe.get());
        }
    }
}
CATCH_LOG()

} // namespace

WslcDebugTransportImpl::WslcDebugTransportImpl(
    wil::unique_handle&& standardInput,
    wil::unique_handle&& standardOutput,
    wil::unique_handle&& standardError,
    wil::unique_handle&& processExitEvent,
    PCWSTR pipeName,
    PCWSTR capabilityToken,
    PCWSTR correlationId,
    PCWSTR providerId)
{
    auto normalizedPipeName = NormalizePipeName(pipeName);
    ValidateCapabilityToken(capabilityToken);
    THROW_HR_IF_NULL(E_POINTER, correlationId);
    THROW_HR_IF(E_INVALIDARG, *correlationId == L'\0');
    THROW_HR_IF_NULL(E_POINTER, providerId);
    THROW_HR_IF(E_INVALIDARG, *providerId == L'\0');

    auto token = wsl::windows::common::string::WideToMultiByte(capabilityToken);
    auto correlation = wsl::windows::common::string::WideToMultiByte(correlationId);
    auto provider = wsl::windows::common::string::WideToMultiByte(providerId);
    const auto stopHandle = stopEvent.get();

    worker = std::thread(
        RunTransport,
        std::move(normalizedPipeName),
        std::move(token),
        std::move(correlation),
        std::move(provider),
        std::move(standardInput),
        std::move(standardOutput),
        std::move(standardError),
        std::move(processExitEvent),
        stopHandle);
}

WslcDebugTransportImpl::~WslcDebugTransportImpl()
{
    stopEvent.SetEvent();
    if (worker.joinable())
    {
        worker.join();
    }
}
