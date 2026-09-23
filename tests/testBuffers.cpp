//
// Created by Stefan Balta on 2026-09-21.
//

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <random>
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

// Transfers larger than the staging rings go through them in pieces. Odd sizes and offsets put the piece boundaries
// anywhere in the buffer.
TEST(buffer_chunked_transfers)
{
    PixelKiln kiln;
    const uint64_t size = 72ull * 1024 * 1024 + 12345;
    const uint64_t offset = 4093;
    std::vector<uint8_t> values(size);
    for (uint64_t i = 0; i < size; i++) {
        values[i] = uint8_t(i * 7 + (i >> 16));
    }
    uint64_t buffer = kiln.createBuffer(size + offset + 100);
    kiln.uploadBuffer(buffer, values.data(), size, offset);
    CHECK(download<uint8_t>(kiln, buffer, size, offset) == values);
    // A range that straddles where the pieces of the full download were cut.
    std::vector<uint8_t> middle = download<uint8_t>(kiln, buffer, 3 << 20, offset + (31ull << 20));
    CHECK(std::equal(middle.begin(), middle.end(), values.begin() + (31ull << 20)));
}

// Random sizes (up to more than one staging piece) at random offsets, checked against a copy kept on the CPU. The
// upload staging ring wraps around many times, and the uploads land in order.
TEST(buffer_random_uploads)
{
    PixelKiln kiln;
    const uint64_t size = 24ull * 1024 * 1024;
    std::vector<uint8_t> mirror(size, 0);
    uint64_t buffer = kiln.createBuffer(size);
    kiln.uploadBuffer(buffer, mirror.data(), size);
    std::mt19937_64 random(1234);
    std::vector<uint8_t> data;
    for (int i = 0; i < 300; i++) {
        uint64_t length = 1 + random() % (i % 10 == 0 ? 6 << 20 : 64 << 10);
        uint64_t offset = random() % (size - length + 1);
        data.resize(length);
        for (uint64_t j = 0; j < length; j++) {
            data[j] = uint8_t(random());
        }
        kiln.uploadBuffer(buffer, data.data(), length, offset);
        std::copy(data.begin(), data.end(), mirror.begin() + int64_t(offset));
    }
    CHECK(download<uint8_t>(kiln, buffer, size) == mirror);
}
