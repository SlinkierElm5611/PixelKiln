//
// Created by Stefan Balta on 2026-09-21.
//

#include <cstdint>
#include <numeric>
#include <vector>

#include "testFramework.h"
#include "testUtil.h"

TEST(buffer_upload_download)
{
    PixelKiln kiln;
    const uint32_t count = 256 * 1024;
    std::vector<uint32_t> values(count);
    std::iota(values.begin(), values.end(), 7u);
    uint64_t buffer = kiln.createBuffer(count * sizeof(uint32_t));
    kiln.uploadBuffer(buffer, values.data(), count * sizeof(uint32_t));
    CHECK(download<uint32_t>(kiln, buffer, count) == values);
    kiln.destroyBuffer(buffer);
}

TEST(buffer_partial_ranges)
{
    PixelKiln kiln;
    std::vector<uint32_t> zeros(64, 0);
    uint64_t buffer = kiln.createBuffer(64 * sizeof(uint32_t));
    kiln.uploadBuffer(buffer, zeros.data(), 64 * sizeof(uint32_t));
    const uint32_t middle[4] = {1, 2, 3, 4};
    kiln.uploadBuffer(buffer, middle, sizeof(middle), 10 * sizeof(uint32_t));

    std::vector<uint32_t> all = download<uint32_t>(kiln, buffer, 64);
    for (uint32_t i = 0; i < 64; i++) {
        uint32_t expected = (i >= 10 && i < 14) ? i - 9 : 0;
        CHECK_EQ(all[i], expected);
    }
    std::vector<uint32_t> part = download<uint32_t>(kiln, buffer, 3, 11 * sizeof(uint32_t));
    CHECK_EQ(part[0], 2u);
    CHECK_EQ(part[1], 3u);
    CHECK_EQ(part[2], 4u);
}

// Consecutive uploads into the same range must land in order.
TEST(buffer_repeated_uploads_in_order)
{
    PixelKiln kiln;
    uint64_t buffer = kiln.createBuffer(1024);
    for (uint32_t i = 0; i < 50; i++) {
        std::vector<uint32_t> values(256, i);
        kiln.uploadBuffer(buffer, values.data(), 1024);
    }
    for (uint32_t value : download<uint32_t>(kiln, buffer, 256)) {
        CHECK_EQ(value, 49u);
    }
}

TEST(buffer_large)
{
    PixelKiln kiln;
    const uint64_t size = 32ull * 1024 * 1024;
    std::vector<uint8_t> values(size);
    for (uint64_t i = 0; i < size; i++) {
        values[i] = uint8_t(i * 31 + (i >> 12));
    }
    uint64_t buffer = kiln.createBuffer(size);
    kiln.uploadBuffer(buffer, values.data(), size);
    CHECK(download<uint8_t>(kiln, buffer, size) == values);
}
