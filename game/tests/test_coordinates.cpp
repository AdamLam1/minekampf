#include <gtest/gtest.h>

#include "core/types.hpp"

using namespace mc;

TEST(Coordinates, ChunkFromBlock) {
    EXPECT_EQ(chunk_from_block({0, 0, 0}).x, 0);
    EXPECT_EQ(chunk_from_block({0, 0, 0}).z, 0);
    EXPECT_EQ(chunk_from_block({16, 0, 0}).x, 1);
    EXPECT_EQ(chunk_from_block({-1, 0, 0}).x, -1);
    EXPECT_EQ(chunk_from_block({-16, 0, 0}).x, -1);
    EXPECT_EQ(chunk_from_block({-17, 0, 0}).x, -2);
    EXPECT_EQ(chunk_from_block({15, 0, 15}).x, 0);
}

TEST(Coordinates, LocalAndSection) {
    EXPECT_EQ(local_x(17), 1);
    EXPECT_EQ(local_x(-1), 15);
    EXPECT_EQ(local_z(-17), 15);
    EXPECT_EQ(section_index(0), 0);
    EXPECT_EQ(section_index(15), 0);
    EXPECT_EQ(section_index(16), 1);
    EXPECT_EQ(section_index(255), 15);
}

TEST(Coordinates, SectionIndex3D) {
    EXPECT_EQ(section_index_3d(0, 0, 0), 0);
    EXPECT_EQ(section_index_3d(1, 0, 0), 1);
    EXPECT_EQ(section_index_3d(0, 0, 1), 16);
    EXPECT_EQ(section_index_3d(0, 1, 0), 256);
}

TEST(Coordinates, ChunkPosDistance) {
    ChunkPos a{0, 0}, b{3, 4};
    EXPECT_EQ(a.distance_sq(b), 25);
}
