#include "amy.h"

#ifndef EXPECT_AMY_BLOCK_SIZE
#error "EXPECT_AMY_BLOCK_SIZE is required"
#endif

#ifndef EXPECT_BLOCK_SIZE_BITS
#error "EXPECT_BLOCK_SIZE_BITS is required"
#endif

#ifndef EXPECT_AMY_SAMPLE_RATE
#error "EXPECT_AMY_SAMPLE_RATE is required"
#endif

_Static_assert(AMY_BLOCK_SIZE == EXPECT_AMY_BLOCK_SIZE,
               "unexpected AMY block size");
_Static_assert(BLOCK_SIZE_BITS == EXPECT_BLOCK_SIZE_BITS,
               "block-size shift does not match the selected block size");
_Static_assert(AMY_SAMPLE_RATE == EXPECT_AMY_SAMPLE_RATE,
               "unexpected AMY sample rate");

int main(void) {
    return 0;
}
