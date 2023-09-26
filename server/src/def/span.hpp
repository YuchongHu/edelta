#ifndef DEDUP_SERVER_SPAN_HPP
#define DEDUP_SERVER_SPAN_HPP

#include "third_party/boost_span.hpp"

namespace dedup {
using boost::span;
using bytes_view = span<const std::byte>;
using mutable_bytes_view = span<std::byte>;
} // namespace dedup
#endif //DEDUP_SERVER_SPAN_HPP
