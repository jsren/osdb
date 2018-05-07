/* btree-tests.cpp - (c) 2018 James Renwick */
#include "ostest/ostest.hpp"
#include <btree.hpp>

using T1 = int;
using T2 = bool;

TEST_SUITE(BtreeSuite);

TEST(BtreeSuite, EmptyTest)
{
    constexpr const size_t order = 4;
    constexpr const size_t leafSize = 8;

    using tree_t = osdb::bplus_tree<T1, T2, order, leafSize>;
    tree_t tree{};

    EXPECT((std::is_same<T1, tree_t::key_type>::value));
    EXPECT((std::is_same<T2, tree_t::value_type>::value));

    EXPECT_EQ(tree.order(), order);
    EXPECT_EQ(tree.leaf_size(), leafSize);
    EXPECT_EQ(tree.height(), 0);
    EXPECT_EQ(tree.size(), 0);

    for (auto& pair : tree.scan_items())
    {
        (void)pair;
        ASSERT(false);
    }
}

TEST(BtreeSuite, AddOne)
{
    constexpr const size_t order = 4;
    constexpr const size_t leafSize = 8;

    osdb::bplus_tree<T1, T2, order, leafSize> tree{};
    tree.add(0, false);

    EXPECT_EQ(tree.order(), order);
    EXPECT_EQ(tree.leaf_size(), leafSize);
    EXPECT_EQ(tree.height(), 0);
    EXPECT_EQ(tree.size(), 1);
}

TEST(BtreeSuite, SearchEmpty)
{
    constexpr const size_t order = 4;
    constexpr const size_t leafSize = 8;
    constexpr const T1 key{0x5AD};

    osdb::bplus_tree<T1, T2, order, leafSize> tree{};

    for (auto& pair : tree.search_range(T1{}, T1{}))
    {
        (void)pair;
        ASSERT(false);
    }
}

TEST(BtreeSuite, SearchOne)
{
    constexpr const size_t order = 4;
    constexpr const size_t leafSize = 8;
    constexpr const T1 key{0x5AD};
    constexpr const T1 value{true};

    osdb::bplus_tree<T1, T2, order, leafSize> tree{};
    tree.add(key, value);

    size_t count = 0;
    for (auto& pair : tree.search_range(key, key))
    {
        ASSERT_LT(count, 1);
        ++count;
        EXPECT_EQ(pair.first, key);
        EXPECT_EQ(pair.second, value);
    }
    EXPECT_EQ(count, 1);
}

TEST(BtreeSuite, SearchTwo)
{
    constexpr const size_t order = 4;
    constexpr const size_t leafSize = 8;
    constexpr const T1 key1{0x5AD};
    constexpr const T1 key2{0xC0FFEE};
    constexpr const T1 value{true};

    std::array<T1, 2> keys{key1, key2};
    static_assert(key1 < key2,"");

    osdb::bplus_tree<T1, T2, order, leafSize> tree{};
    tree.add(key1, value);
    tree.add(key2, value);

    size_t count = 0;
    for (auto& pair : tree.search_range(key1, key2))
    {
        ASSERT_LT(count, 2);
        EXPECT(pair.first == keys[count]);
        EXPECT_EQ(pair.second, value);
        ++count;
    }
    EXPECT_EQ(count, 2);
}

TEST(BtreeSuite, SearchSame)
{
    constexpr const size_t order = 4;
    constexpr const size_t leafSize = 8;
    constexpr const T1 key{0x5AD};
    constexpr const T1 value{true};

    osdb::bplus_tree<T1, T2, order, leafSize> tree{};
    tree.add(key, value);
    tree.add(key, value);

    size_t count = 0;
    for (auto& pair : tree.search_range(key, key))
    {
        ASSERT_LT(count, 2);
        ++count;
        EXPECT_EQ(pair.first, key);
        EXPECT_EQ(pair.second, value);
    }
    EXPECT_EQ(count, 2);
}

TEST(BtreeSuite, FillLeafSameScan)
{
    constexpr const size_t order = 4;
    constexpr const size_t leafSize = 8;

    constexpr const T1 key{0x5AD};
    constexpr const T2 value{true};

    osdb::bplus_tree<T1, T2, order, leafSize> tree{};
    for (size_t i = 0; i < leafSize; i++) {
        tree.add(key, value);
    }
    EXPECT_EQ(tree.order(), order);
    EXPECT_EQ(tree.leaf_size(), leafSize);
    EXPECT_EQ(tree.height(), 0);
    EXPECT_EQ(tree.size(), leafSize);

    size_t count = 0;
    for (auto& pair : tree.scan_items())
    {
        ASSERT_LT(count, leafSize);
        ++count;
        EXPECT_EQ(pair.first, key);
        EXPECT_EQ(pair.second, value);
    }
    EXPECT_EQ(count, leafSize);
}
