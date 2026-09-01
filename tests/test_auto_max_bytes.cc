#include "xrdhover/file_session.hh"
#include "xrdhover/run_config.hh"

#include <algorithm>
#include <stdexcept>

#include <gtest/gtest.h>

using xrdhover::ComputeAutoMaxBytes;
using xrdhover::ComputeBucketCapacity;
using xrdhover::ComputeOpBytes;
using xrdhover::ResolveRunConfig;
using xrdhover::RunConfig;

TEST(AutoMaxBytes, ScalesWithMaxInflight) {
    RunConfig cfg;
    cfg.target_rate_bytes_per_s = 50ull << 20;  // 50 MiB/s
    cfg.chunk_size = 1 << 20;
    cfg.max_inflight = 8;
    const uint64_t a = ComputeAutoMaxBytes(cfg);
    cfg.max_inflight = 32;
    const uint64_t b = ComputeAutoMaxBytes(cfg);
    EXPECT_GT(a, b);
    EXPECT_GE(a, static_cast<uint64_t>(cfg.chunk_size) * xrdhover::kAutoMaxFloorChunks);
    const uint64_t aggregate_cap = static_cast<uint64_t>(
        static_cast<double>(cfg.target_rate_bytes_per_s) * xrdhover::kRateHeadroomSec);
    EXPECT_LE(a, std::min(aggregate_cap, xrdhover::kAutoMaxHardCapBytes));
}

TEST(AutoMaxBytes, RespectsHardCap) {
    RunConfig cfg;
    cfg.target_rate_bytes_per_s = 500ull * 1000 * 1000;  // 500 MB/s SI
    cfg.chunk_size = 1 << 20;
    cfg.max_inflight = 1;  // amortize would be huge without the hard cap
    EXPECT_LE(ComputeAutoMaxBytes(cfg), xrdhover::kAutoMaxHardCapBytes);
}

TEST(AutoMaxBytes, ResolveSetsMaxBytes) {
    RunConfig cfg;
    cfg.target_rate_bytes_per_s = 50ull << 20;
    cfg.chunk_size = 1 << 20;
    cfg.max_inflight = 16;
    cfg.max_bytes_auto = true;
    ResolveRunConfig(cfg);
    EXPECT_GT(cfg.max_bytes, 0u);
    EXPECT_EQ(cfg.max_bytes, ComputeAutoMaxBytes(cfg));
}

TEST(AutoMaxBytes, UncappedRejectsAuto) {
    RunConfig cfg;
    cfg.max_bytes_auto = true;
    cfg.target_rate_bytes_per_s = 0;
    EXPECT_THROW(ResolveRunConfig(cfg), std::runtime_error);
}

TEST(AutoMaxBytes, UncappedRejectsZeroMaxBytes) {
    RunConfig cfg;
    cfg.max_bytes_auto = false;
    cfg.max_bytes = 0;
    cfg.target_rate_bytes_per_s = 0;
    EXPECT_THROW(ResolveRunConfig(cfg), std::runtime_error);
}

TEST(AutoMaxBytes, UncappedKeepsExplicitMaxBytes) {
    RunConfig cfg;
    cfg.max_bytes_auto = false;
    cfg.max_bytes = 32ull << 20;
    cfg.target_rate_bytes_per_s = 0;
    ResolveRunConfig(cfg);
    EXPECT_EQ(cfg.max_bytes, 32ull << 20);
}

TEST(BucketCapacity, OneReadOp) {
    RunConfig cfg;
    cfg.target_rate_bytes_per_s = 10ull << 20;
    cfg.max_inflight = 16;
    cfg.max_bytes = 32ull << 20;
    cfg.chunk_size = 8ull << 20;
    cfg.pattern = xrdhover::PatternType::Sequential;
    EXPECT_EQ(ComputeBucketCapacity(cfg), cfg.chunk_size);
    EXPECT_EQ(ComputeOpBytes(cfg, false), cfg.chunk_size);
    EXPECT_LT(ComputeBucketCapacity(cfg), cfg.max_bytes * cfg.max_inflight);
}

TEST(BucketCapacity, VectorUsesFullVectorRead) {
    RunConfig cfg;
    cfg.target_rate_bytes_per_s = 10ull << 20;
    cfg.chunk_size = 1 << 20;
    cfg.vector_chunks = 8;
    cfg.pattern = xrdhover::PatternType::Vector;
    EXPECT_EQ(ComputeBucketCapacity(cfg), cfg.chunk_size * cfg.vector_chunks);
}

TEST(BucketCapacity, UncappedIsZero) {
    RunConfig cfg;
    cfg.target_rate_bytes_per_s = 0;
    cfg.chunk_size = 1 << 20;
    EXPECT_EQ(ComputeBucketCapacity(cfg), 0u);
}

TEST(SessionOpBytes, ReadAndVector) {
    xrdhover::FileSessionOptions o;
    o.chunk_size = 8 << 20;
    o.vector_chunks = 0;
    EXPECT_EQ(xrdhover::SessionOpBytes(o), 8u << 20);
    o.vector_chunks = 4;
    EXPECT_EQ(xrdhover::SessionOpBytes(o), 32u << 20);
}
