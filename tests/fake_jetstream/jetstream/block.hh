#pragma once

#include "jetstream/types.hh"

namespace Jetstream {

struct Block {
    struct Impl;
    struct Config {
        virtual ~Config() = default;
    };
};

}  // namespace Jetstream
