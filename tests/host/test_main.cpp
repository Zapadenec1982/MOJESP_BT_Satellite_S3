/**
 * @file test_main.cpp
 * @brief doctest entry point for ModESP host unit tests.
 *
 * All test cases live in separate test_*.cpp files. main() is spelled out rather than
 * generated (DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN) for one reason: the ETL error handler has
 * to be installed before the first test constructs a container, and the run has to fail if
 * an ETL assertion fired that no test declared — see etl_error_recorder.h.
 */

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include "etl_error_recorder.h"

int main(int argc, char** argv) {
    etl::error_handler::set_callback<host_etl::record>();

    doctest::Context ctx(argc, argv);
    int rc = ctx.run();
    if (ctx.shouldExit()) return rc;

    if (host_etl::count() != host_etl::expected()) {
        std::printf("\n[ETL] %u assertion(s) fired but %u were declared by tests (last: %s)\n"
                    "[ETL] an undeclared ETL assertion is a container misuse the firmware would abort on\n",
                    host_etl::count(), host_etl::expected(), host_etl::last().c_str());
        return rc != 0 ? rc : 1;
    }
    return rc;
}
