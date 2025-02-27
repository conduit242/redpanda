/*
 * Copyright 2021 Redpanda Data, Inc.
 *
 * Use of this software is governed by the Business Source License
 * included in the file licenses/BSL.md
 *
 * As of the Change Date specified in that file, in accordance with
 * the Business Source License, use of this software will be governed
 * by the Apache License, Version 2.0
 */

#include "error.h"

#include "pandaproxy/error.h"

namespace pandaproxy::json {

namespace {

struct error_category final : std::error_category {
    const char* name() const noexcept override { return "pandaproxy::json"; }
    std::string message(int ev) const override {
        switch (static_cast<error_code>(ev)) {
        case error_code::invalid_json:
            return "invalid json";
        }
        return "(unrecognized error)";
    }
    std::error_condition
    default_error_condition(int ec) const noexcept override {
        switch (static_cast<error_code>(ec)) {
        case error_code::invalid_json:
            return reply_error_code::unprocessable_entity;
        }
        return {};
    }
};

const error_category ppj_error_category{};

}; // namespace

std::error_code make_error_code(error_code e) {
    return {static_cast<int>(e), ppj_error_category};
}

} // namespace pandaproxy::json
