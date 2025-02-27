// Copyright 2020 Redpanda Data, Inc.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.md
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0

#include "model/record.h"
#include "seastarx.h"
#include "storage/ntp_config.h"
#include "storage/tests/disk_log_builder_fixture.h"
#include "storage/tests/utils/random_batch.h"
#include "test_utils/fixture.h"

#include <seastar/core/file.hh>
#include <seastar/core/reactor.hh>

#include <optional>

FIXTURE_TEST(kitchen_sink, log_builder_fixture) {
    using namespace storage; // NOLINT

    auto batch = test::make_random_batch(model::offset(104), 1, false);

    b | start() | add_segment(0)
      | add_random_batch(0, 100, maybe_compress_batches::yes)
      | add_random_batch(100, 2, maybe_compress_batches::yes) | add_segment(102)
      | add_random_batch(102, 2, maybe_compress_batches::yes) | add_segment(104)
      | add_batch(std::move(batch)) | add_random_batches(105, 3);

    auto stats = get_stats().get0();

    b | stop();
    BOOST_TEST(stats.seg_count == 3);
    BOOST_TEST(stats.batch_count == 7);
    BOOST_TEST(stats.record_count >= 105);
}

static void do_write_zeroes(ss::sstring name) {
    auto fd = ss::open_file_dma(
                name, ss::open_flags::create | ss::open_flags::rw)
                .get0();
    auto out = ss::make_file_output_stream(std::move(fd)).get0();
    ss::temporary_buffer<char> b(4096);
    std::memset(b.get_write(), 0, 4096);
    out.write(b.get(), b.size()).get();
    out.flush().get();
    out.close().get();
}

static void do_write_garbage(ss::sstring name) {
    auto fd = ss::open_file_dma(
                name, ss::open_flags::create | ss::open_flags::rw)
                .get0();
    auto out = ss::make_file_output_stream(std::move(fd)).get0();
    const auto b = random_generators::gen_alphanum_string(100);
    out.write(b.data(), b.size()).get();
    out.flush().get();
    out.close().get();
}

FIXTURE_TEST(test_valid_segment_name_with_zeroes_data, log_builder_fixture) {
    using namespace storage; // NOLINT
    auto ntp = model::ntp(
      model::ns("test.ns"), model::topic("zeroes"), model::partition_id(66));
    storage::ntp_config ncfg(ntp, b.get_log_config().base_dir);
    const ss::sstring dir = ncfg.work_directory();
    // 1. write valid segment names in the namespace with 0 data so crc passes
    recursive_touch_directory(dir).get();
    do_write_zeroes(ssx::sformat("{}/270-1850-v1.log", dir));
    do_write_zeroes(ssx::sformat("{}/270-1850-v1.base_index", dir));
    do_write_garbage(ssx::sformat("{}/271-1850-v1.log", dir));
    do_write_garbage(ssx::sformat("{}/271-1850-v1.base_index", dir));

    b | start(ntp);
    auto stats = get_stats().get0();
    b | stop();

    BOOST_REQUIRE(
      !ss::file_exists(ssx::sformat("{}/270-1850-v1.log", dir)).get0());
    BOOST_TEST(stats.seg_count == 0);
    BOOST_TEST(stats.batch_count == 0);
    BOOST_TEST(stats.record_count >= 0);
}

FIXTURE_TEST(iterator_invalidation, log_builder_fixture) {
    using namespace storage; // NOLINT
    constexpr const model::record_batch_type configuration
      = model::record_batch_type::raft_configuration;
    constexpr const model::record_batch_type data
      = model::record_batch_type::raft_data;

    b | start() | add_segment(0)
      | add_random_batch(0, 1, maybe_compress_batches::yes, configuration)
      | add_segment(1)
      | add_random_batch(1, 1, maybe_compress_batches::yes, data)
      | add_segment(2)
      | add_random_batch(2, 1, maybe_compress_batches::yes, configuration)
      | add_segment(3)
      | add_random_batch(3, 1, maybe_compress_batches::yes, data);

    auto data_batches = b.consume(log_reader_config(
                                    model::offset(0),
                                    model::model_limits<model::offset>::max(),
                                    0,
                                    std::numeric_limits<size_t>::max(),
                                    ss::default_priority_class(),
                                    data,
                                    std::nullopt,
                                    std::nullopt))
                          .get0();
    auto config_batches = b.consume(log_reader_config(
                                      model::offset(0),
                                      model::model_limits<model::offset>::max(),
                                      0,
                                      std::numeric_limits<size_t>::max(),
                                      ss::default_priority_class(),
                                      configuration,
                                      std::nullopt,
                                      std::nullopt))
                            .get0();
    auto all_batches = b.consume().get0();
    b | stop();
    BOOST_REQUIRE_EQUAL(data_batches.size(), 2);
    BOOST_REQUIRE_EQUAL(config_batches.size(), 2);
    BOOST_REQUIRE_EQUAL(all_batches.size(), 4);
}
