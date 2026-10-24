/**
 * @file etl_error_recorder.h
 * @brief HOST BUILD: where every failed ETL assertion lands.
 *
 * The host runner compiles with ETL_LOG_ERRORS + ETL_CHECK_PUSH_POP, the same two
 * definitions the firmware uses (root CMakeLists.txt), so a container misuse that would
 * abort the panel is seen here first. test_main.cpp installs record() as the
 * etl::error_handler callback before any test runs.
 *
 * The firmware's callback aborts, because a plain ETL_ASSERT continues into the path the
 * check was guarding once the callback returns. The host cannot abort a test suite, but it
 * compiles with exceptions on — so a test that has to stop ETL at the assert (a full
 * etl::unordered_map would go on into its exhausted pool) opens a ThrowScope and the
 * callback throws instead. Everything else just counts: at the end of the run main()
 * compares count() with expected(), so an ETL assertion nobody asked for fails the suite.
 */
#pragma once

#include "etl/error_handler.h"
#include "etl/exception.h"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace host_etl {

inline unsigned&    count()    { static unsigned n = 0; return n; }
inline unsigned&    expected() { static unsigned n = 0; return n; }
inline std::string& last()     { static std::string s; return s; }
inline bool&        throwing() { static bool t = false; return t; }

inline void record(const etl::exception& e) {
    count()++;
    last() = e.what();
    std::printf("[ETL] assertion: %s (%s:%d)\n", e.what(), e.file_name(), (int)e.line_number());
    if (throwing()) throw std::runtime_error(e.what());
}

/// A test that deliberately trips N assertions declares them, so the end-of-run check
/// still catches the ones nobody declared.
struct Expect {
    explicit Expect(unsigned n) { expected() += n; }
};

/// While alive, the callback throws std::runtime_error out of the assert site.
struct ThrowScope {
    ThrowScope()  { throwing() = true; }
    ~ThrowScope() { throwing() = false; }
};

} // namespace host_etl
