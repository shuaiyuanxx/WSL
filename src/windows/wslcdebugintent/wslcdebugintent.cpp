// wslcdebugintent.exe
//
// Extension-bundled WSLC Debug Intent broker client.
//
// It CoCreates the WSLCSessionManager coclass, QIs IWSLCDebugIntentManager,
// configures the proxy for COM impersonation (so the service captures the
// caller's real SID/PID/image identity), and calls RegisterDebugIntent.
//
// It emits exactly one line of JSON to stdout describing the registered
// intent (intentId, endpoint pipe, capability token hex, correlationId,
// expiry) and then stays alive as a long-lived broker: the parent (the VS Code
// extension) owns its lifetime and may send "cancel" on stdin to cancel the
// intent, or simply terminate/close stdin to let the intent expire.
//
// Inputs are taken from fixed command-line arguments only. Manifest-derived
// values are passed as argv and never shell-evaluated.
//
// Build: x64 Debug. Standalone; links only ole32/oleaut32. It includes the
// service-generated wslc.h for the interface/struct/coclass definitions.

#include <windows.h>
#include <combaseapi.h>
#include <objidl.h>   // IClientSecurity
#include <rpcdce.h>
#include <bcrypt.h>
#include <string>
#include <string_view>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cwchar>

// Service-generated interface/struct/coclass definitions.
#include "wslc.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "rpcrt4.lib")

namespace {

// Reimplementation of wsl::windows::common::security::ConfigureForCOMImpersonation
// so this tool stays standalone (no dependency on the heavy common lib / PCH).
// Enables dynamic cloaking + impersonate level on the proxy so the service can
// capture the real caller identity when RegisterDebugIntent runs.
HRESULT ConfigureForCOMImpersonation(IUnknown* instance)
{
    IClientSecurity* clientSecurity = nullptr;
    HRESULT hr = instance->QueryInterface(IID_PPV_ARGS(&clientSecurity));
    if (FAILED(hr))
    {
        return hr;
    }

    DWORD authnSvc = 0, authzSvc = 0, authnLvl = 0, capabilities = 0;
    hr = clientSecurity->QueryBlanket(
        instance, &authnSvc, &authzSvc, nullptr, &authnLvl, nullptr, nullptr, &capabilities);
    if (SUCCEEDED(hr))
    {
        capabilities &= ~EOAC_STATIC_CLOAKING;
        capabilities |= EOAC_DYNAMIC_CLOAKING;
        hr = clientSecurity->SetBlanket(
            instance, authnSvc, authzSvc, nullptr, authnLvl, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, capabilities);
    }

    clientSecurity->Release();
    return hr;
}

std::string Narrow(const wchar_t* value)
{
    if (value == nullptr || *value == L'\0')
    {
        return {};
    }
    int required = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 0)
    {
        return {};
    }
    std::string result(static_cast<size_t>(required - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), required, nullptr, nullptr);
    return result;
}

std::wstring Widen(const std::string& value)
{
    if (value.empty())
    {
        return {};
    }
    int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (required <= 0)
    {
        return {};
    }
    std::wstring result(static_cast<size_t>(required - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), required);
    return result;
}

std::string JsonEscape(std::string_view value)
{
    std::string result;
    result.reserve(value.size() + 8);
    for (char ch : value)
    {
        switch (ch)
        {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20)
            {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", ch);
                result += buffer;
            }
            else
            {
                result += ch;
            }
            break;
        }
    }
    return result;
}

// Match wslcsession's GuidToString(..., GuidToStringFlags::None): lowercase,
// hyphenated, and without braces.
std::string GuidToCorrelationId(const GUID& guid)
{
    char buffer[37];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%08lx-%04hx-%04hx-%02x%02x-%02x%02x%02x%02x%02x%02x",
        guid.Data1,
        guid.Data2,
        guid.Data3,
        guid.Data4[0],
        guid.Data4[1],
        guid.Data4[2],
        guid.Data4[3],
        guid.Data4[4],
        guid.Data4[5],
        guid.Data4[6],
        guid.Data4[7]);
    return std::string(buffer);
}

std::string TokenToHex(const BYTE* token, size_t length)
{
    static const char* digits = "0123456789abcdef";
    std::string result;
    result.reserve(length * 2);
    for (size_t i = 0; i < length; ++i)
    {
        result.push_back(digits[token[i] >> 4]);
        result.push_back(digits[token[i] & 0x0F]);
    }
    return result;
}

struct Options
{
    std::wstring expectedHostFileName;
    std::wstring expectedHostFilePath;
    std::string providerId;
    std::string imageReference;
    std::string imageSha256Hex;
    std::string targetProgram;
    std::string targetWorkingDirectory;
    std::string debuggerPath;
    ULONG expiryMs = 0;
};

const wchar_t* ArgValue(int argc, wchar_t** argv, int& index)
{
    if (index + 1 >= argc)
    {
        return nullptr;
    }
    return argv[++index];
}

int Fail(const char* message, HRESULT hr = S_OK)
{
    if (hr != S_OK)
    {
        std::fprintf(stderr, "[wslcdebugintent] %s (hr=0x%08lx)\n", message, static_cast<unsigned long>(hr));
    }
    else
    {
        std::fprintf(stderr, "[wslcdebugintent] %s\n", message);
    }
    return 1;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    Options options;

    for (int i = 1; i < argc; ++i)
    {
        std::wstring_view arg = argv[i];
        auto takeNarrow = [&](std::string& target) {
            const wchar_t* value = ArgValue(argc, argv, i);
            if (value != nullptr)
            {
                target = Narrow(value);
            }
        };

        if (arg == L"--host-file-name")
        {
            const wchar_t* value = ArgValue(argc, argv, i);
            if (value)
            {
                options.expectedHostFileName = value;
            }
        }
        else if (arg == L"--host-file-path")
        {
            const wchar_t* value = ArgValue(argc, argv, i);
            if (value)
            {
                options.expectedHostFilePath = value;
            }
        }
        else if (arg == L"--provider")
        {
            takeNarrow(options.providerId);
        }
        else if (arg == L"--image-reference")
        {
            takeNarrow(options.imageReference);
        }
        else if (arg == L"--image-sha256")
        {
            takeNarrow(options.imageSha256Hex);
        }
        else if (arg == L"--target-program")
        {
            takeNarrow(options.targetProgram);
        }
        else if (arg == L"--target-cwd")
        {
            takeNarrow(options.targetWorkingDirectory);
        }
        else if (arg == L"--debugger-path")
        {
            takeNarrow(options.debuggerPath);
        }
        else if (arg == L"--expiry-ms")
        {
            const wchar_t* value = ArgValue(argc, argv, i);
            if (value)
            {
                options.expiryMs = static_cast<ULONG>(_wtoi(value));
            }
        }
        else
        {
            std::fprintf(stderr, "[wslcdebugintent] Unknown argument: %ls\n", argv[i]);
            return 1;
        }
    }

    if (options.expectedHostFileName.empty() || options.providerId.empty() ||
        options.targetProgram.empty() || options.debuggerPath.empty())
    {
        return Fail(
            "Missing required arguments. Need --host-file-name, --provider, --target-program, --debugger-path.");
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        return Fail("CoInitializeEx failed", hr);
    }

    int exitCode = 0;
    {
        IWSLCDebugIntentManager* intentManager = nullptr;
        hr = CoCreateInstance(
            __uuidof(WSLCSessionManager),
            nullptr,
            CLSCTX_LOCAL_SERVER,
            __uuidof(IWSLCDebugIntentManager),
            reinterpret_cast<void**>(&intentManager));
        if (FAILED(hr))
        {
            CoUninitialize();
            return Fail("CoCreateInstance(WSLCSessionManager -> IWSLCDebugIntentManager) failed", hr);
        }

        hr = ConfigureForCOMImpersonation(intentManager);
        if (FAILED(hr))
        {
            intentManager->Release();
            CoUninitialize();
            return Fail("ConfigureForCOMImpersonation failed", hr);
        }

        WSLCDebugIntentRequest request{};
        request.Version = WSLC_DEBUG_INTENT_VERSION_1;
        request.ExpectedHostFileName = options.expectedHostFileName.c_str();
        request.ExpectedHostFilePath =
            options.expectedHostFilePath.empty() ? nullptr : options.expectedHostFilePath.c_str();
        request.ProviderId = options.providerId.c_str();
        request.ImageReference = options.imageReference.empty() ? nullptr : options.imageReference.c_str();
        request.ImageSha256Hex = options.imageSha256Hex.empty() ? nullptr : options.imageSha256Hex.c_str();
        request.TargetProgram = options.targetProgram.c_str();
        request.TargetWorkingDirectory =
            options.targetWorkingDirectory.empty() ? nullptr : options.targetWorkingDirectory.c_str();
        request.DebuggerPath = options.debuggerPath.c_str();
        request.ExpiryMs = options.expiryMs;

        WSLCDebugIntentResult result{};
        hr = intentManager->RegisterDebugIntent(&request, &result);
        if (FAILED(hr))
        {
            intentManager->Release();
            CoUninitialize();
            return Fail("RegisterDebugIntent failed", hr);
        }

        const std::string intentId = GuidToCorrelationId(result.IntentId);
        const std::string correlationId = intentId; // wslcsession derives correlationId from the intent GUID.
        const std::string endpoint = Narrow(result.Endpoint);
        const std::string tokenHex = TokenToHex(result.Token, WSLC_DEBUG_INTENT_TOKEN_BYTES);

        std::string json = "{";
        json += "\"intentId\":\"" + JsonEscape(intentId) + "\",";
        json += "\"correlationId\":\"" + JsonEscape(correlationId) + "\",";
        json += "\"endpoint\":\"" + JsonEscape(endpoint) + "\",";
        json += "\"token\":\"" + JsonEscape(tokenHex) + "\",";
        json += "\"provider\":\"" + JsonEscape(options.providerId) + "\",";
        json += "\"expiryFileTime\":" + std::to_string(result.ExpiryFileTime);
        json += "}";

        // Emit exactly one JSON line, then flush so the parent can read it
        // immediately even though we keep running.
        std::printf("%s\n", json.c_str());
        std::fflush(stdout);

        // Zero the secret from our copy of the result struct as soon as it has
        // been serialized for the parent.
        SecureZeroMemory(result.Token, sizeof(result.Token));

        // Long-lived broker loop. The parent owns our lifetime: it may send
        // "cancel" on stdin to explicitly cancel, or close stdin / terminate us
        // to let the intent expire on its own. Cancel authorization on the
        // service side is bound to THIS process (SID + PID + creation time), so
        // cancellation must originate here.
        char line[256];
        while (std::fgets(line, sizeof(line), stdin) != nullptr)
        {
            std::string_view command(line);
            while (!command.empty() && (command.back() == '\n' || command.back() == '\r' || command.back() == ' '))
            {
                command.remove_suffix(1);
            }

            if (command == "cancel")
            {
                HRESULT cancelHr = intentManager->CancelDebugIntent(result.IntentId);
                if (SUCCEEDED(cancelHr))
                {
                    std::printf("{\"cancelled\":true}\n");
                }
                else
                {
                    // A claimed/expired intent may report not-found; treat as
                    // benign for the demo.
                    std::printf(
                        "{\"cancelled\":false,\"hr\":\"0x%08lx\"}\n",
                        static_cast<unsigned long>(cancelHr));
                }
                std::fflush(stdout);
                break;
            }
            // Unknown commands are ignored; the broker keeps waiting.
        }

        intentManager->Release();
    }

    CoUninitialize();
    return exitCode;
}
