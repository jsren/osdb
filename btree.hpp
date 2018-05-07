/* btree.hpp - (c) 2018 James Renwick */
#pragma once
#include <memory>
#include <array>
#include <functional>
#include <type_traits>
#include <algorithm>

namespace osdb
{
    template<typename T, typename Key, size_t Order, size_t LeafSize>
    class bplus_node;

    template<typename Key, typename Value, size_t Order, size_t LeafSize>
    class bplus_tree;

    template<typename Leaf>
    class leaf_iterator;


    template<typename Key, typename Value, size_t Order, size_t LeafSize>
    class bplus_leaf
    {
        using node_type = bplus_node<Key, Value, Order, LeafSize>;
        using tree_type = bplus_tree<Key, Value, Order, LeafSize>;
        friend node_type;
        friend tree_type;
        friend leaf_iterator<bplus_leaf>;
        friend leaf_iterator<const bplus_leaf>;

    public:
        using value_type = std::pair<Key, Value>;
        using key_type = Key;

    private:
        static_assert(Order % 2 == 0, "Order must be a factor of two");

        node_type* parent{};

        bplus_leaf* leftLeaf;
        bplus_leaf* rightLeaf;

        std::array<std::pair<Key, Value>, LeafSize> items{};
        size_t count{};

        bplus_leaf(node_type* parent, bplus_leaf* left, bplus_leaf* right)
            : parent(parent), leftLeaf(left), rightLeaf(right) { }

        size_t add(Key key, Value value)
        {
            items[count++] = std::pair<Key, Value>(
                std::move(key), std::move(value));

            std::sort(std::begin(items),
                count != LeafSize ? (std::begin(items) + count) : std::end(items));
            return 0;
        }
    };


    template<typename Key, typename Value, size_t Order, size_t LeafSize>
    class bplus_node
    {
        friend bplus_tree<Key, Value, Order, LeafSize>;
        static_assert(Order % 2 == 0, "Order must be a factor of two");

    public:
        using key_type = Key;

    private:
        using leaf_type = bplus_leaf<Key, Value, Order, LeafSize>;

        bplus_node* parent{};
        bool hasLeaves{};

        bplus_node* leftSibling;
        bplus_node* rightSibling;

        union element
        {
            leaf_type* leaf;
            bplus_node* node;
        };

        std::array<key_type, Order> keys{};
        std::array<element, Order + 1> nodes{};

        bplus_node(bplus_node* parent, bool hasLeaves, bplus_node* left, bplus_node* right)
            : parent(parent), hasLeaves(hasLeaves), leftSibling(left), rightSibling(right) { }

        size_t add(Key key, Value value, leaf_type*& first, leaf_type*& last)
        {
            size_t i = 0;
            for (; i < Order; i++) {
                if (nodes[i+1].node == nullptr || key < keys[i]) break;
            }

            if (hasLeaves)
            {
                if (nodes[i].leaf == nullptr)
                {
                    nodes[i].leaf = new leaf_type(this, nullptr, nullptr);
                    first = nodes[i].leaf;
                    last = nodes[i].leaf;
                }
                return nodes[i].leaf->add(std::move(key), std::move(value));
            }
            else
            {
                if (nodes[i].node == nullptr) {
                    nodes[i].node = new bplus_node(this, true, nullptr, nullptr);
                }
                return nodes[i].node->add(std::move(key), std::move(value), first, last);
            }
        }
    };

    template<typename Leaf>
    class leaf_iterator
    {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = typename Leaf::value_type;
        using difference_type = std::ptrdiff_t;
        using pointer = value_type*;
        using reference = value_type&;

    private:
        Leaf* leaf;
        size_t index;

    public:
        leaf_iterator(Leaf* leaf, size_t index) noexcept
            : leaf(leaf), index(index) { }

        auto& operator *() noexcept {
            return leaf->items[index];
        }
        auto* operator ->() noexcept {
            return &leaf->items[index];
        }

        bool operator ==(const leaf_iterator& other) const noexcept {
            return leaf == other.leaf && index == other.index;
        }
        bool operator !=(const leaf_iterator& other) const noexcept {
            return !(operator ==(other));
        }

        leaf_iterator& operator ++() noexcept
        {
            if (++index == leaf->count)
            {
                leaf = leaf->rightLeaf;
                index = 0;
            }
            return *this;
        }

        leaf_iterator operator ++(int) noexcept
        {
            leaf_iterator out(leaf, index);
            if (++index == leaf->count)
            {
                leaf = leaf->rightLeaf;
                index = 0;
            }
            return out;
        }
    };

    template<typename Leaf>
    class leaf_iterable
    {
        Leaf* first;
        size_t start;
        Leaf* last;
        size_t _end;

    public:
        leaf_iterable(Leaf* first, size_t start, Leaf* last, size_t end)
            : first(first), start(start), last(last), _end(end) { }

        leaf_iterator<Leaf> begin() noexcept {
            return leaf_iterator<Leaf>(first, start);
        }
        leaf_iterator<Leaf> end() noexcept {
            return leaf_iterator<Leaf>(last, _end);
        }
    };

    template<typename Key, typename Value, size_t Order, size_t LeafSize>
    class bplus_tree
    {
        static_assert(Order % 2 == 0, "Order must be a factor of two");

    public:
        using key_type = Key;
        using value_type = Value;

    private:
        using node_type = bplus_node<Key, Value, Order, LeafSize>;
        using leaf_type = typename node_type::leaf_type;

        size_t _height{};
        size_t _size{};
        node_type root{nullptr, true, nullptr, nullptr};

        leaf_type* firstLeaf;
        leaf_type* lastLeaf;

    public:
        bplus_tree() = default;


        constexpr size_t order() const noexcept {
            return Order;
        }

        constexpr size_t leaf_size() const noexcept {
            return LeafSize;
        }

        size_t height() const noexcept {
            return _height;
        }

        size_t size() const noexcept {
            return _size;
        }

        void add(Key key, Value value)
        {
            _height = root.add(std::move(key), std::move(value),
                firstLeaf, lastLeaf);
            _size++;
        }

        auto scan_items() const
        {
            return leaf_iterable<const leaf_type>(
                firstLeaf, 0, nullptr, 0);
        }

        struct range_start { };
        struct range_end { };


        template<typename T, typename Y>
        auto search_range(const T& start, const Y& end,
            bool inclusiveStart = true, bool inclusiveEnd = true) const
        {
            static_assert(!std::is_same<T, range_end>::value,"");
            static_assert(!std::is_same<Y, range_start>::value,"");

            bool fromStart = std::is_same<T, range_start>::value;
            bool fromEnd = std::is_same<Y, range_end>::value;

            const leaf_type* firstLeaf = fromStart ?
                this->firstLeaf : find_leaf(start, inclusiveStart);
            const leaf_type* lastLeaf = fromEnd ?
                this->lastLeaf : find_leaf(end, inclusiveEnd);

            size_t firstIndex = 0;
            if (firstLeaf != nullptr)
            {
                for (; firstIndex < firstLeaf->count; firstIndex++)
                {
                    if ((inclusiveStart && firstLeaf->items[firstIndex].first == start) ||
                        firstLeaf->items[firstIndex].first > start) break;
                }
                if (firstIndex == firstLeaf->count)
                {
                    firstLeaf = firstLeaf->rightLeaf;
                    firstIndex = 0;
                }
            }
            // Get the index of one past the final item
            size_t lastIndex = 0;
            if (lastLeaf != nullptr && lastLeaf->count != 0)
            {
                lastIndex = lastLeaf->count;
                for (; lastIndex != 0; lastIndex--)
                {
                    if ((inclusiveEnd && lastLeaf->items[lastIndex-1].first == end) ||
                        lastLeaf->items[lastIndex-1].first < end) break;
                }
                if (lastIndex == lastLeaf->count)
                {
                    lastLeaf = lastLeaf->rightLeaf;
                    lastIndex = 0;
                }
            }
            return leaf_iterable<const leaf_type>(firstLeaf,
                firstIndex, lastLeaf, lastIndex);
        }

    private:
        const leaf_type* find_leaf(const key_type& value, bool inclusive) const
        {
            const node_type* node = &root;
            while (true)
            {
                size_t i = 0;
                for (; i < Order; i++)
                {
                    if (value < node->keys[i]) continue;
                    else if (!inclusive && (node->keys[i] == value)) continue;
                    else break;
                }
                if (node->hasLeaves) return node->nodes[i].leaf;
                else node = node->nodes[i].node;
            }
            return nullptr;
        }
    };
}
