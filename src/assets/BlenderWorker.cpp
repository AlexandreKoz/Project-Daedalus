#include "assets/BlenderWorker.h"

#include "core/Json.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace daedalus
{
namespace
{
[[nodiscard]] std::string utf8(const std::filesystem::path& path)
{
    const std::u8string value = path.u8string();
    return std::string(value.begin(), value.end());
}

[[nodiscard]] std::vector<std::string> string_array(const JsonValue& root, std::string_view key)
{
    std::vector<std::string> result;
    const JsonValue* value = root.find(key);
    if (value == nullptr || !value->is_array()) return result;
    for (const JsonValue& entry : value->as_array())
        if (entry.is_string()) result.push_back(entry.as_string());
    return result;
}

struct ProcessOutcome
{
    bool started = false;
    bool timed_out = false;
    bool cancelled = false;
    int exit_code = -1;
    std::string failure;
};

#ifdef _WIN32
class ScopedHandle final
{
public:
    explicit ScopedHandle(HANDLE handle = nullptr) noexcept : handle_(handle) {}
    ~ScopedHandle() { reset(); }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    void reset(HANDLE handle = nullptr) noexcept
    {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
        handle_ = handle;
    }
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] bool valid() const noexcept { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }

private:
    HANDLE handle_ = nullptr;
};

[[nodiscard]] std::wstring widen(const std::string& value)
{
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) throw std::runtime_error("argument is not valid UTF-8");
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), required);
    return result;
}

ProcessOutcome run_process(const std::filesystem::path& executable, const std::vector<std::string>& arguments,
                           const std::filesystem::path& log_path, std::chrono::milliseconds timeout, const std::atomic<bool>* cancel)
{
    ProcessOutcome outcome;
    std::string command_line;
    for (std::size_t index = 0; index < arguments.size(); ++index)
    {
        if (index != 0U) command_line.push_back(' ');
        command_line += quote_windows_argument(arguments[index]);
    }
    std::wstring wide_command = widen(command_line);

    SECURITY_ATTRIBUTES inherit{};
    inherit.nLength = sizeof(inherit);
    inherit.bInheritHandle = TRUE;
    ScopedHandle log(CreateFileW(log_path.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr));
    ScopedHandle null_input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!log.valid() || !null_input.valid())
    {
        outcome.failure = "cannot create the Blender log/input handles (Win32 error " + std::to_string(GetLastError()) + ")";
        return outcome;
    }
    ScopedHandle job(CreateJobObjectW(nullptr, nullptr));
    if (!job.valid())
    {
        outcome.failure = "CreateJobObjectW failed (Win32 error " + std::to_string(GetLastError()) + ")";
        return outcome;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) == FALSE)
    {
        outcome.failure = "SetInformationJobObject failed (Win32 error " + std::to_string(GetLastError()) + ")";
        return outcome;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = null_input.get();
    startup.hStdOutput = log.get();
    startup.hStdError = log.get();
    PROCESS_INFORMATION process{};
    const std::wstring application = executable.wstring();
    if (CreateProcessW(application.c_str(), wide_command.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
                       nullptr, nullptr, &startup, &process) == FALSE)
    {
        outcome.failure = "CreateProcessW failed for " + utf8(executable) + " (Win32 error " + std::to_string(GetLastError()) + ")";
        return outcome;
    }
    ScopedHandle process_handle(process.hProcess);
    ScopedHandle thread_handle(process.hThread);
    if (AssignProcessToJobObject(job.get(), process_handle.get()) == FALSE)
    {
        TerminateProcess(process_handle.get(), 1);
        outcome.failure = "AssignProcessToJobObject failed (Win32 error " + std::to_string(GetLastError()) + ")";
        return outcome;
    }
    ResumeThread(thread_handle.get());
    outcome.started = true;

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;)
    {
        const DWORD wait = WaitForSingleObject(process_handle.get(), 100);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_FAILED)
        {
            TerminateJobObject(job.get(), 1);
            outcome.failure = "WaitForSingleObject failed while waiting for Blender";
            return outcome;
        }
        if (cancel != nullptr && cancel->load())
        {
            TerminateJobObject(job.get(), 1);
            WaitForSingleObject(process_handle.get(), 5000);
            outcome.cancelled = true;
            return outcome;
        }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            TerminateJobObject(job.get(), 1);
            WaitForSingleObject(process_handle.get(), 5000);
            outcome.timed_out = true;
            return outcome;
        }
    }
    DWORD exit_code = 0;
    if (GetExitCodeProcess(process_handle.get(), &exit_code) == FALSE)
    {
        outcome.failure = "GetExitCodeProcess failed";
        return outcome;
    }
    outcome.exit_code = static_cast<int>(exit_code);
    return outcome;
}
#else
ProcessOutcome run_process(const std::filesystem::path& executable, const std::vector<std::string>& arguments,
                           const std::filesystem::path& log_path, std::chrono::milliseconds timeout, const std::atomic<bool>* cancel)
{
    ProcessOutcome outcome;
    std::vector<std::string> storage = arguments;
    std::vector<char*> argv;
    argv.reserve(storage.size() + 1U);
    for (std::string& argument : storage) argv.push_back(argument.data());
    argv.push_back(nullptr);
    const std::string executable_path = utf8(executable);
    const std::string log_file = utf8(log_path);

    const pid_t pid = fork();
    if (pid < 0)
    {
        outcome.failure = "fork failed";
        return outcome;
    }
    if (pid == 0)
    {
        setpgid(0, 0);
        const int log_descriptor = open(log_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        const int null_descriptor = open("/dev/null", O_RDONLY);
        if (log_descriptor < 0 || null_descriptor < 0) _exit(126);
        dup2(null_descriptor, STDIN_FILENO);
        dup2(log_descriptor, STDOUT_FILENO);
        dup2(log_descriptor, STDERR_FILENO);
        execv(executable_path.c_str(), argv.data());
        _exit(127);
    }
    setpgid(pid, pid);
    outcome.started = true;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;)
    {
        int status = 0;
        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid)
        {
            if (WIFEXITED(status)) outcome.exit_code = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) outcome.failure = "Blender terminated by signal " + std::to_string(WTERMSIG(status));
            if (outcome.exit_code == 127) outcome.failure = "execv failed for " + executable_path;
            return outcome;
        }
        if (waited < 0)
        {
            outcome.failure = "waitpid failed";
            return outcome;
        }
        const bool cancel_requested = cancel != nullptr && cancel->load();
        if (cancel_requested || std::chrono::steady_clock::now() >= deadline)
        {
            kill(-pid, SIGKILL);
            waitpid(pid, &status, 0);
            outcome.cancelled = cancel_requested;
            outcome.timed_out = !cancel_requested;
            return outcome;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
#endif
}

std::string quote_windows_argument(std::string_view argument)
{
    if (!argument.empty() && argument.find_first_of(" \t\n\v\"") == std::string_view::npos) return std::string(argument);
    std::string result = "\"";
    for (std::size_t index = 0;; ++index)
    {
        std::size_t backslashes = 0;
        while (index < argument.size() && argument[index] == '\\')
        {
            ++index;
            ++backslashes;
        }
        if (index == argument.size())
        {
            result.append(backslashes * 2U, '\\');
            break;
        }
        if (argument[index] == '"')
        {
            result.append(backslashes * 2U + 1U, '\\');
            result.push_back('"');
        }
        else
        {
            result.append(backslashes, '\\');
            result.push_back(argument[index]);
        }
    }
    result.push_back('"');
    return result;
}

std::vector<std::string> blender_command_arguments(const BlenderConversionRequest& request, const std::filesystem::path& executable)
{
    const std::filesystem::path glb = request.output_directory / "converted.glb";
    const std::filesystem::path manifest = request.output_directory / "manifest.json";
    // --factory-startup ignores user add-ons/preferences so conversions are reproducible;
    // --python-exit-code makes script exceptions visible as a non-zero exit code.
    return {utf8(executable), "--background", "--factory-startup", "--python-exit-code", "3",
            "--python", utf8(request.script), "--", utf8(request.source), utf8(glb), utf8(manifest)};
}

std::optional<std::filesystem::path> locate_blender_executable(const std::filesystem::path& configured)
{
    std::error_code error;
    if (!configured.empty())
    {
        if (std::filesystem::is_regular_file(configured, error) && !error) return configured;
        return std::nullopt;
    }
    const char* path_variable = std::getenv("PATH");
    if (path_variable == nullptr) return std::nullopt;
#ifdef _WIN32
    constexpr char separator = ';';
    const char* names[] = {"blender.exe"};
#else
    constexpr char separator = ':';
    const char* names[] = {"blender"};
#endif
    std::stringstream stream{std::string(path_variable)};
    std::string directory;
    while (std::getline(stream, directory, separator))
    {
        if (directory.empty()) continue;
        for (const char* name : names)
        {
            const std::filesystem::path candidate = std::filesystem::path(directory) / name;
            if (std::filesystem::is_regular_file(candidate, error) && !error) return candidate;
        }
    }
    return std::nullopt;
}

BlenderConversionResult run_blender_conversion(const BlenderConversionRequest& request)
{
    BlenderConversionResult result;
    std::error_code error;
    if (!std::filesystem::is_regular_file(request.source, error) || error)
    {
        result.failure = "Blender source file does not exist: " + utf8(request.source);
        return result;
    }
    if (!std::filesystem::is_regular_file(request.script, error) || error)
    {
        result.failure = "Blender export script is missing: " + utf8(request.script);
        return result;
    }
    const std::optional<std::filesystem::path> executable = locate_blender_executable(request.blender_executable);
    if (!executable.has_value())
    {
        result.failure = request.blender_executable.empty()
            ? "Blender was not found on PATH; pass --blender <path-to-blender executable>"
            : "Blender executable not found: " + utf8(request.blender_executable);
        return result;
    }
    if (request.timeout.count() <= 0)
    {
        result.failure = "Blender timeout must be positive";
        return result;
    }
    result.executable = *executable;
    std::filesystem::create_directories(request.output_directory, error);
    if (error)
    {
        result.failure = "cannot create the conversion directory " + utf8(request.output_directory) + ": " + error.message();
        return result;
    }
    result.glb_path = request.output_directory / "converted.glb";
    result.manifest_path = request.output_directory / "manifest.json";
    result.log_path = request.output_directory / "blender.log";
    std::filesystem::remove(result.glb_path, error);
    std::filesystem::remove(result.manifest_path, error);

    const ProcessOutcome outcome = run_process(*executable, blender_command_arguments(request, *executable), result.log_path,
                                               std::chrono::duration_cast<std::chrono::milliseconds>(request.timeout), request.cancel);
    result.exit_code = outcome.exit_code;
    result.timed_out = outcome.timed_out;
    result.cancelled = outcome.cancelled;
    if (!outcome.started || !outcome.failure.empty())
    {
        result.failure = outcome.failure.empty() ? "Blender could not be started" : outcome.failure;
        return result;
    }
    if (outcome.timed_out)
    {
        result.failure = "Blender conversion timed out after " + std::to_string(request.timeout.count()) + " s (process tree terminated)";
        return result;
    }
    if (outcome.cancelled)
    {
        result.failure = "Blender conversion was cancelled";
        return result;
    }

    // The manifest is read even on failure: the script records its own diagnostics there.
    if (std::filesystem::is_regular_file(result.manifest_path, error))
    {
        try
        {
            std::ifstream input(result.manifest_path, std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            if (text.size() > 16U * 1024U * 1024U) throw std::runtime_error("manifest is unreasonably large");
            const JsonValue manifest = parse_json(text);
            if (const JsonValue* version = manifest.find("blender_version"); version != nullptr && version->is_string())
                result.blender_version = version->as_string();
            if (const JsonValue* script = manifest.find("script_version"); script != nullptr && script->is_string())
                result.script_version = script->as_string();
            result.warnings = string_array(manifest, "warnings");
            result.unsupported = string_array(manifest, "unsupported");
            result.approximations = string_array(manifest, "approximations");
        }
        catch (const std::exception& parse_error)
        {
            result.warnings.push_back(std::string("conversion manifest could not be parsed: ") + parse_error.what());
        }
    }
    else
    {
        result.warnings.push_back("Blender did not write a conversion manifest");
    }
    if (outcome.exit_code != 0)
    {
        result.failure = "Blender exited with code " + std::to_string(outcome.exit_code) + "; see " + utf8(result.log_path);
        return result;
    }
    if (!std::filesystem::is_regular_file(result.glb_path, error))
    {
        result.failure = "Blender exited successfully but produced no GLB";
        return result;
    }
    const std::uintmax_t size = std::filesystem::file_size(result.glb_path, error);
    if (error || size == 0U || size > request.maximum_output_bytes)
    {
        result.failure = "converted GLB is empty or exceeds the configured output size limit";
        return result;
    }
    if (result.script_version != kBlenderExportScriptVersion)
        result.warnings.push_back("conversion manifest reports script version '" + result.script_version + "', expected '" +
                                  std::string(kBlenderExportScriptVersion) + "'");
    result.success = true;
    return result;
}
}
