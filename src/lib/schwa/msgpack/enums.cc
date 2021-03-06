/* -*- Mode: C++; indent-tabs-mode: nil -*- */
#include <schwa/msgpack/enums.h>

#include <iostream>


namespace schwa {
namespace msgpack {

std::ostream &
operator <<(std::ostream &out, const WireType type) {
  return out << static_cast<int>(type);
}

}  // namespace msgpack
}  // namespace schwa
