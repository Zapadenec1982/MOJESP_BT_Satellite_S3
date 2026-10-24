/**
 * @file test_etl_asserts.cpp
 * @brief ETL assertions are real, and they reach the handler.
 *
 * With exceptions off and no ETL_LOG_ERRORS, ETL_ASSERT compiled to nothing: a full
 * etl::vector wrote past its end and a full etl::unordered_map inserted into an exhausted
 * pool, in silence. These cases pin the two definitions the build now carries
 * (ETL_LOG_ERRORS, ETL_CHECK_PUSH_POP) and the handler that receives what they report.
 *
 * About the text. Neither build defines ETL_VERBOSE_ERRORS — it would put ETL's header paths
 * into flash — so exception::what() is the TERSE form, which is a code, not a sentence:
 * "<file id><letter>", the file id from etl/file_error_numbers.h and the letter counting the
 * exceptions in that header. 17A is vector_full, 17B vector_empty, 16A unordered_map_full.
 * The codes are asserted here because they are what the panel's log will actually show, so
 * whoever reads "ETL assertion: 17A" at the bench can find it written down somewhere.
 */

#include "doctest.h"
#include "etl_error_recorder.h"

#include "etl/vector.h"
#include "etl/unordered_map.h"

#include <stdexcept>
#include <utility>

TEST_CASE("a full etl::vector reports push_back and keeps its size") {
    host_etl::Expect declared(1);
    const unsigned before = host_etl::count();

    etl::vector<int, 2> v;
    v.push_back(1);
    v.push_back(2);
    CHECK(host_etl::count() == before);

    // ETL_CHECK_PUSH_POP: the assert is an ETL_ASSERT_OR_RETURN — the handler runs, then
    // push_back returns without writing. Nothing to catch, nothing corrupted.
    v.push_back(3);
    CHECK(host_etl::count() == before + 1);
    CHECK(v.size() == 2);
    CHECK(v[1] == 2);
    CHECK(host_etl::last() == "17A");   // etl::vector_full, terse
}

TEST_CASE("a full etl::unordered_map reports insert instead of overrunning its pool") {
    host_etl::Expect declared(1);

    using Map = etl::unordered_map<int, int, 2>;
    Map m;
    m.insert(Map::value_type(1, 10));
    m.insert(Map::value_type(2, 20));
    const unsigned before = host_etl::count();

    // A plain ETL_ASSERT: once the handler returns, insert() carries on into the pool it
    // has just been told is empty. On the panel the handler aborts; here it throws.
    {
        host_etl::ThrowScope stop;
        const Map::value_type third(3, 30);
        CHECK_THROWS_AS(m.insert(third), std::runtime_error);
    }
    CHECK(host_etl::count() == before + 1);
    CHECK(m.size() == 2);
    CHECK(m.find(3) == m.end());
    CHECK(m.find(1)->second == 10);
    CHECK(host_etl::last() == "16A");   // etl::unordered_map_full, terse
}

TEST_CASE("an empty etl::vector reports pop_back") {
    host_etl::Expect declared(1);
    const unsigned before = host_etl::count();

    etl::vector<int, 2> v;
    v.pop_back();
    CHECK(host_etl::count() == before + 1);
    CHECK(v.empty());
    CHECK(host_etl::last() == "17B");   // etl::vector_empty, terse
}
