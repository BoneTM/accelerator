#include <config.h>
#include <filesystem>
#include <limits>
#include <memory>

#include <fmt/core.h>

#include "google_breakpad/processor/basic_source_line_resolver.h"
#include "google_breakpad/processor/minidump_processor.h"
#include "google_breakpad/processor/process_state.h"
#include "processor/simple_symbol_supplier.h"
#include "processor/stackwalk_common.h"

static bool process_minidump_file(const char* minidump_path, const char* output_path)
{
    if (!minidump_path || !output_path)
    {
        fmt::println("Error: Invalid parameters");
        return false;
    }

    if (!std::filesystem::exists(minidump_path))
    {
        fmt::println("Error: Minidump file not found: {}", minidump_path);
        return false;
    }

    std::unique_ptr<google_breakpad::SimpleSymbolSupplier> symbol_supplier;
    google_breakpad::BasicSourceLineResolver resolver;
    google_breakpad::MinidumpProcessor minidump_processor(symbol_supplier.get(), &resolver);

    google_breakpad::MinidumpThreadList::set_max_threads(std::numeric_limits<uint32_t>::max());
    google_breakpad::MinidumpMemoryList::set_max_regions(std::numeric_limits<uint32_t>::max());

    google_breakpad::Minidump mini_dump(minidump_path);
    if (!mini_dump.Read())
    {
        fmt::println("Error: Failed to read minidump from {}", minidump_path);
        return false;
    }

    google_breakpad::ProcessState process_state;
    if (minidump_processor.Process(&mini_dump, &process_state) != google_breakpad::PROCESS_OK)
    {
        fmt::println("Error: Failed to process minidump {}", minidump_path);
        return false;
    }

    FILE* output_file = fopen(output_path, "w");
    if (!output_file)
    {
        fmt::println("Error: Failed to open output file: {}", output_path);
        return false;
    }

    FILE* old_stdout = stdout;
    stdout           = output_file;
    PrintProcessState(process_state, true, false, &resolver);
    fflush(stdout);
    stdout = old_stdout;
    fclose(output_file);

    fmt::println("Successfully processed minidump. Output written to: {}", output_path);
    return true;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        fmt::println("Usage: {} <minidump_file>", argv[0]);
        return 1;
    }

    std::string output_path = std::string(argv[1]) + ".txt";

    return process_minidump_file(argv[1], output_path.c_str()) ? 0 : 1;
}
