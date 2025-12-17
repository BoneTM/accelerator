#include <filesystem>
#include <config.h>

#include "client/linux/handler/exception_handler.h"
#include "third_party/lss/linux_syscall_support.h"

#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <array>
#include <ctime>
#include <fmt/base.h>
#include <fmt/chrono.h>
#include <fmt/core.h>
#include <thread>

#include <sys/stat.h>

#include "google_breakpad/processor/basic_source_line_resolver.h"
#include "google_breakpad/processor/minidump_processor.h"
#include "google_breakpad/processor/process_state.h"
#include "google_breakpad/processor/stack_frame.h"
#include "google_breakpad/processor/stack_frame_cpu.h"
#include "google_breakpad/processor/call_stack.h"
#include "processor/simple_symbol_supplier.h"
#include "processor/stackwalk_common.h"
#include "processor/pathname_stripper.h"

static constexpr std::array<int, 5> kExceptionSignals = { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS };
void (*SignalHandler)(int, siginfo_t*, void*);
constexpr std::string_view dumpPath                 = "./breakpad";
google_breakpad::ExceptionHandler* exceptionHandler = nullptr;

bool g_should_stop = false;

static void kill_myself()
{
    kill(getpid(), SIGKILL);
}

static bool ProcessMinidump(const char* minidump_path, const std::string& output_base_path)
{
    std::unique_ptr<google_breakpad::SimpleSymbolSupplier> symbol_supplier;
    google_breakpad::BasicSourceLineResolver resolver;
    google_breakpad::MinidumpProcessor minidump_processor(symbol_supplier.get(), &resolver);

    google_breakpad::MinidumpThreadList::set_max_threads(std::numeric_limits<uint32_t>::max());
    google_breakpad::MinidumpMemoryList::set_max_regions(std::numeric_limits<uint32_t>::max());

    google_breakpad::Minidump mini_dump(minidump_path);
    if (!mini_dump.Read())
        return false;

    google_breakpad::ProcessState process_state;
    if (minidump_processor.Process(&mini_dump, &process_state) != google_breakpad::PROCESS_OK)
        return false;

    std::string info_path = output_base_path + ".txt";
    FILE* info_file       = fopen(info_path.c_str(), "w");
    if (info_file)
    {
        FILE* old_stdout = stdout;
        stdout           = info_file;
        PrintProcessState(process_state, true, false, &resolver);
        fflush(stdout);
        stdout = old_stdout;
        fclose(info_file);
    }

    try
    {
        std::filesystem::remove(minidump_path);
    }
    catch (...)
    {
    }

    return true;
}

static void CleanupOldFiles()
{
    if (!std::filesystem::exists(dumpPath) || !std::filesystem::is_directory(dumpPath))
        return;

    for (const auto& entry : std::filesystem::directory_iterator(dumpPath))
    {
        if (!entry.is_regular_file())
            continue;

        auto ext = entry.path().extension();
        if (ext != ".txt" && ext != ".dmp")
            continue;

        try
        {
            auto file_time     = std::filesystem::last_write_time(entry);
            auto file_age      = std::filesystem::file_time_type::clock::now() - file_time;
            auto file_age_days = std::chrono::duration_cast<std::chrono::hours>(file_age).count() / 24;

            if (file_age_days >= 30)
            {
                std::filesystem::remove(entry.path());
            }
        }
        catch (...)
        {
        }
    }
}

static void ProcessExistingMinidumps()
{
    if (!std::filesystem::exists(dumpPath) || !std::filesystem::is_directory(dumpPath))
        return;

    for (const auto& entry : std::filesystem::directory_iterator(dumpPath))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".dmp")
            continue;

        std::string output_base_path = fmt::format("./breakpad/{}", entry.path().stem().string());
        ProcessMinidump(entry.path().string().c_str(), output_base_path);
    }
}

static bool DumpCallback(const google_breakpad::MinidumpDescriptor& descriptor, void* context, bool succeeded)
{
    g_should_stop = true;

    try
    {
        std::filesystem::create_directories("./breakpad");
    }
    catch (const std::exception& e)
    {
        FILE* error_log = fopen("./breakpad_error.log", "a");
        if (error_log)
        {
            fprintf(error_log, "Failed to create breakpad directory: %s\n", e.what());
            fclose(error_log);
        }
        kill_myself();
        return false;
    }

    if (!succeeded)
    {
        FILE* error_log = fopen("./breakpad/crash_error.log", "a");
        if (error_log)
        {
            fprintf(error_log, "Failed to write minidump\n");
            fclose(error_log);
        }
        kill_myself();
        return false;
    }

    auto t         = std::time(nullptr);
    auto timestamp = fmt::format("{:%Y-%m-%d-%H-%M-%S}", fmt::localtime(t));
    auto base_path = fmt::format("./breakpad/crashdump_{}", timestamp);

    bool result = ProcessMinidump(descriptor.path(), base_path);
    if (!result)
    {
        kill_myself();
    }

    return result;
}

extern "C" __attribute__((visibility("default"))) bool InitBreakpad()
{
    struct stat st = { 0 };
    if (stat(dumpPath.data(), &st) == -1)
    {
        if (mkdir(dumpPath.data(), 0770) == -1)
        {
            fmt::println("[Breakpad] Failed to create file path: {}", dumpPath.data());
            return false;
        }
    }
    else
    {
        chmod(dumpPath.data(), 0770);
    }

    CleanupOldFiles();
    ProcessExistingMinidumps();

    google_breakpad::MinidumpDescriptor descriptor(dumpPath.data());
    exceptionHandler = new google_breakpad::ExceptionHandler(descriptor, nullptr, DumpCallback, nullptr, true, -1);

    struct sigaction oact;
    sigaction(SIGSEGV, NULL, &oact);
    SignalHandler = oact.sa_sigaction;

    std::thread(
      []()
      {
          using namespace std::chrono_literals;
          while (!g_should_stop)
          {
              bool needs_to_replace = false;
              struct sigaction oact;

              for (auto signal : kExceptionSignals)
              {
                  sigaction(signal, NULL, &oact);

                  if (oact.sa_sigaction != SignalHandler)
                  {
                      needs_to_replace = true;
                      break;
                  }
              }

              if (!needs_to_replace)
              {
                  std::this_thread::sleep_for(500ms);
                  return;
              }

              struct sigaction act;
              memset(&act, 0, sizeof(act));
              sigemptyset(&act.sa_mask);

              for (auto signal : kExceptionSignals)
                  sigaddset(&act.sa_mask, signal);

              act.sa_sigaction = SignalHandler;
              act.sa_flags     = SA_ONSTACK | SA_SIGINFO;

              for (auto signal : kExceptionSignals)
                  sigaction(signal, &act, NULL);
          }
      })
      .detach();

    return true;
}