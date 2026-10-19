#pragma once

#include "jetstream/types.hh"

namespace Jetstream {

struct Module {
    struct Config {
        virtual ~Config() = default;
    };
};

}  // namespace Jetstream
