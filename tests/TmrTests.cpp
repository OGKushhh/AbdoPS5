// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-017: TMR controller unit tests.

#include "graphics/host_gpu/tmrController.h"
#include "graphics/host_gpu/mmioDispatcher.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

using TmrController = Libs::Graphics::TmrController;
using MmioDispatcher = Libs::Graphics::MmioDispatcher;
namespace TmrMmio = Libs::Graphics::TmrMmio;
namespace TmrEntry = Libs::Graphics::TmrEntry;

int g_test_failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "TmrTests: FAILED: %s\n", message);
        ++g_test_failures;
    } else {
        std::printf("TmrTests: passed: %s\n", message);
    }
}

// Test: TMR starts with all entries permissive.
void TestInitialPermissive() {
    TmrController tmr;
    for (uint32_t i = 0; i < TmrEntry::MAX_ENTRIES; ++i) {
        Check(tmr.IsEntryPermissive(i), "All entries start permissive");
    }
}

// Test: MMIO read/write of the index register.
void TestIndexRegister() {
    TmrController tmr;

    // Write to the index register.
    const uint32_t index_value = TmrEntry::FIELD_CONFIG | (5 * TmrEntry::ENTRY_SIZE); // TMR_CONFIG(5)
    Check(tmr.MmioWrite(TmrMmio::ECAM_BASE + TmrMmio::INDEX_OFF, &index_value, sizeof(index_value)),
          "MmioWrite INDEX accepted");
    Check(tmr.GetIndex() == index_value, "Index register stores the value");

    // Read back the index register.
    uint32_t read_value = 0;
    Check(tmr.MmioRead(TmrMmio::ECAM_BASE + TmrMmio::INDEX_OFF, &read_value, sizeof(read_value)),
          "MmioRead INDEX accepted");
    Check(read_value == index_value, "INDEX read returns the value we wrote");
}

// Test: Write to TMR_CONFIG(5) and read it back.
void TestConfigReadWrite() {
    TmrController tmr;

    // Set index to TMR_CONFIG(5) = 5 * 0x10 + 0x08 = 0x58
    const uint32_t index = 5 * TmrEntry::ENTRY_SIZE + TmrEntry::FIELD_CONFIG;
    tmr.MmioWrite(TmrMmio::ECAM_BASE + TmrMmio::INDEX_OFF, &index, sizeof(index));

    // Write config = 0 (disable entry)
    const uint32_t disable_value = 0;
    tmr.MmioWrite(TmrMmio::ECAM_BASE + TmrMmio::DATA_OFF, &disable_value, sizeof(disable_value));
    Check(!tmr.IsEntryActive(5), "Entry 5 disabled after config=0");

    // Write config = 0x3F07 (permissive)
    const uint32_t permissive_value = TmrEntry::CFG_PERMISSIVE;
    tmr.MmioWrite(TmrMmio::ECAM_BASE + TmrMmio::DATA_OFF, &permissive_value, sizeof(permissive_value));
    Check(tmr.IsEntryPermissive(5), "Entry 5 permissive after config=0x3F07");

    // Read config back
    uint32_t read_config = 0;
    tmr.MmioRead(TmrMmio::ECAM_BASE + TmrMmio::DATA_OFF, &read_config, sizeof(read_config));
    Check(read_config == TmrEntry::CFG_PERMISSIVE, "Config read returns 0x3F07");
}

// Test: MMIO out-of-range returns false.
void TestMmioOutOfRange() {
    TmrController tmr;
    uint32_t value = 0;

    Check(!tmr.MmioRead(TmrMmio::ECAM_BASE - 1, &value, sizeof(value)),
          "Read before ECAM base returns false");
    Check(!tmr.MmioRead(TmrMmio::ECAM_BASE + TmrMmio::MMIO_RANGE, &value, sizeof(value)),
          "Read after ECAM range returns false");
}

// Test: DisableAll sets all entries to 0.
void TestDisableAll() {
    TmrController tmr;
    tmr.DisableAll();

    for (uint32_t i = 0; i < TmrEntry::MAX_ENTRIES; ++i) {
        Check(!tmr.IsEntryActive(i), "All entries disabled after DisableAll");
    }
}

// Test: SetAllPermissive sets all entries to 0x3F07.
void TestSetAllPermissive() {
    TmrController tmr;
    tmr.DisableAll();
    tmr.SetAllPermissive();

    for (uint32_t i = 0; i < TmrEntry::MAX_ENTRIES; ++i) {
        Check(tmr.IsEntryPermissive(i), "All entries permissive after SetAllPermissive");
    }
}

// Test: Dispatcher routes TMR accesses.
void TestDispatcherRouting() {
    MmioDispatcher::Instance().Clear();

    TmrController tmr;
    tmr.RegisterWithDispatcher();

    // Write to the index register via the dispatcher.
    const uint32_t index_value = 0;
    Check(MmioDispatcher::Instance().DispatchWrite(
              TmrMmio::ECAM_BASE + TmrMmio::INDEX_OFF, &index_value, sizeof(index_value)),
          "Dispatcher routes write to TMR INDEX register");

    // Read TMR_CONFIG(0) via the dispatcher.
    uint32_t read_value = 0;
    Check(MmioDispatcher::Instance().DispatchRead(
              TmrMmio::ECAM_BASE + TmrMmio::DATA_OFF, &read_value, sizeof(read_value)),
          "Dispatcher routes read from TMR DATA register");
    Check(read_value == TmrEntry::CFG_PERMISSIVE, "TMR_CONFIG(0) is permissive via dispatcher");

    // Out-of-range.
    Check(!MmioDispatcher::Instance().DispatchRead(0x1000, &read_value, sizeof(read_value)),
          "Dispatcher returns false for out-of-range address");

    MmioDispatcher::Instance().Clear();
}

} // namespace

int main() {
    TestInitialPermissive();
    TestIndexRegister();
    TestConfigReadWrite();
    TestMmioOutOfRange();
    TestDisableAll();
    TestSetAllPermissive();
    TestDispatcherRouting();

    if (g_test_failures == 0) {
        std::printf("\nTmrTests: ALL PASSED\n");
        return 0;
    }
    std::printf("\nTmrTests: %d FAILED\n", g_test_failures);
    return 1;
}
