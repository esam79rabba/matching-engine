// adaptive_radix_tree.cpp
//
// The AdaptiveRadixTree is fully implemented as a header-only template
// in engine/adaptive_radix_tree.h. This translation unit exists to:
//   1. Verify the header compiles cleanly.
//   2. Provide a home for any future non-template helpers.

#include "engine/adaptive_radix_tree.h"

// Explicit instantiation for the primary use case (PriceLevel storage).
// This catches template compilation errors at library build time rather
// than deferring them to test or application TUs.
#include "engine/order_book.h"

namespace engine {

template class AdaptiveRadixTree<PriceLevel>;

}  // namespace engine
