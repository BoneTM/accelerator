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
#include <chrono>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <fmt/base.h>
#include <fmt/core.h>
#include <thread>

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
// 使用游戏可执行文件旁的目录，与 parasite 的 CS2 崩溃包收集路径保持一致。
// 在初始化时固定绝对路径，避免启动目录或游戏后续 chdir 影响输出位置。
static std::string dumpPath;
google_breakpad::ExceptionHandler* exceptionHandler = nullptr;

bool g_should_stop = false;

static void kill_myself()
{
    kill(getpid(), SIGKILL);
}

static bool ProcessMinidump(const char* minidump_path)
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

    // 与离线 minidump_processor 保持一致：UUID.dmp 与 UUID.dmp.txt 一一对应。
    std::string info_path = std::string(minidump_path) + ".txt";
    FILE* info_file       = fopen(info_path.c_str(), "w");
    if (!info_file)
        return false;

    FILE* old_stdout = stdout;
    stdout           = info_file;
    PrintProcessState(process_state, true, false, &resolver);
    const bool flushed = fflush(info_file) == 0;
    const bool written = ferror(info_file) == 0;
    stdout             = old_stdout;
    const bool closed  = fclose(info_file) == 0;

    // dmp 是原始现场，无论文本解析是否成功都保留，由 parasite 归档、补传并清理。
    return flushed && written && closed;
}

static bool DumpCallback(const google_breakpad::MinidumpDescriptor& descriptor, void* context, bool succeeded)
{
    g_should_stop = true;

    if (!succeeded)
    {
        FILE* error_log = fopen((dumpPath + "/crash_error.log").c_str(), "a");
        if (error_log)
        {
            fprintf(error_log, "Failed to write minidump\n");
            fclose(error_log);
        }
        kill_myself();
        return false;
    }

    // 文本只是辅助产物，失败时仍保留已生成的 dmp，并向 Breakpad 返回转储成功。
    // 不因为文本失败发送 SIGKILL，否则 parasite 可能把真实崩溃误判为 OOM。
    ProcessMinidump(descriptor.path());
    return succeeded;
}

extern "C" __attribute__((visibility("default"))) bool InitBreakpad()
{
    std::error_code error;
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error)
    {
        fmt::println("[Breakpad] Failed to locate game executable: {}", error.message());
        return false;
    }
    dumpPath = (executable.parent_path() / "breakpad").string();
    std::filesystem::create_directories(dumpPath, error);
    if (error)
    {
        fmt::println("[Breakpad] Failed to create file path: {}: {}", dumpPath, error.message());
        return false;
    }
    chmod(dumpPath.c_str(), 0770);

    // 不在新运行里重写历史报告或按文件年龄删除现场：parasite 依靠启动基线和
    // mtime 判断 runId，且尚未上传成功的文件必须保留供后续重试。

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
