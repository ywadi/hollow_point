// Exercises the vendored libraries that nothing else calls yet.
//
// `05-verification-status.md` recorded enkiTS / meshoptimizer as "compile and
// link, but no code calls them -- their APIs have never been exercised". These
// tests close that gap, and because the suite is built and run for both
// targets, they close it on Windows as well as Linux.
//
// Bucket: fast. No GPU, no filesystem, milliseconds.

#include <doctest/doctest.h>

#include <TaskScheduler.h>
#include <meshoptimizer.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace {

struct Vertex {
    float x, y, z;

    bool operator==(const Vertex& o) const { return x == o.x && y == o.y && z == o.z; }
};

} // namespace

TEST_CASE("meshoptimizer welds duplicate vertices" * doctest::test_suite("libraries")) {
    // A quad drawn as two triangles with a shared edge, expressed as six
    // separate vertices. Four of them are unique; the remap should say so.
    const std::vector<Vertex> unindexed = {
        {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {1.f, 1.f, 0.f},
        {0.f, 0.f, 0.f}, {1.f, 1.f, 0.f}, {0.f, 1.f, 0.f},
    };

    std::vector<unsigned int> remap(unindexed.size());
    const size_t unique =
        meshopt_generateVertexRemap(remap.data(), nullptr, unindexed.size(), unindexed.data(),
                                    unindexed.size(), sizeof(Vertex));

    REQUIRE(unique == 4);

    std::vector<Vertex> vertices(unique);
    std::vector<unsigned int> indices(unindexed.size());
    meshopt_remapVertexBuffer(vertices.data(), unindexed.data(), unindexed.size(), sizeof(Vertex),
                              remap.data());
    meshopt_remapIndexBuffer(indices.data(), nullptr, unindexed.size(), remap.data());

    // The index buffer must still describe the original six corners.
    REQUIRE(indices.size() == unindexed.size());
    for (size_t i = 0; i < unindexed.size(); ++i) {
        CHECK(indices[i] < unique);
        CHECK(vertices[indices[i]] == unindexed[i]);
    }
}

TEST_CASE("meshoptimizer vertex cache optimisation preserves the triangles" *
          doctest::test_suite("libraries")) {
    // Reordering for the vertex cache must permute triangles, never change
    // which vertices each one references.
    const std::vector<unsigned int> indices = {0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4};
    const size_t vertex_count = 8;

    std::vector<unsigned int> optimized(indices.size());
    meshopt_optimizeVertexCache(optimized.data(), indices.data(), indices.size(), vertex_count);

    REQUIRE(optimized.size() == indices.size());
    for (unsigned int i : optimized)
        CHECK(i < vertex_count);

    // Same multiset of triangles, order-independent.
    auto triangles = [](const std::vector<unsigned int>& idx) {
        std::vector<std::vector<unsigned int>> tris;
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            std::vector<unsigned int> t = {idx[i], idx[i + 1], idx[i + 2]};
            std::sort(t.begin(), t.end());
            tris.push_back(t);
        }
        std::sort(tris.begin(), tris.end());
        return tris;
    };
    CHECK(triangles(optimized) == triangles(indices));
}

TEST_CASE("enkiTS runs every element of a task set exactly once" *
          doctest::test_suite("libraries")) {
    enki::TaskScheduler scheduler;
    scheduler.Initialize();

    constexpr uint32_t kSetSize = 10000;
    std::vector<std::atomic<uint32_t>> visits(kSetSize);
    for (auto& v : visits)
        v.store(0, std::memory_order_relaxed);

    std::atomic<uint32_t> total{0};

    enki::TaskSet task(kSetSize, [&](enki::TaskSetPartition range, uint32_t /*threadnum*/) {
        for (uint32_t i = range.start; i < range.end; ++i) {
            visits[i].fetch_add(1, std::memory_order_relaxed);
            total.fetch_add(1, std::memory_order_relaxed);
        }
    });

    scheduler.AddTaskSetToPipe(&task);
    scheduler.WaitforTask(&task);

    // Exactly once each: a partitioning bug shows up as a zero or a two, and
    // the total catches any element skipped entirely.
    CHECK(total.load() == kSetSize);
    for (uint32_t i = 0; i < kSetSize; ++i) {
        REQUIRE_MESSAGE(visits[i].load() == 1, "element ", i, " visited ", visits[i].load(),
                        " times");
    }

    scheduler.WaitforAllAndShutdown();
}
