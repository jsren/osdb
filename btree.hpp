/* btree.hpp - (c) 2018 James Renwick */
#pragma once
#include <memory>
#include <array>
#include <functional>
#include <type_traits>
#include <algorithm>

namespace osdb
{
    struct range_start { };
    struct range_end { };

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
        static_assert(Order <= std::numeric_limits<size_t>::max() / 2, "");

    public:
        using key_type = Key;

    private:
        using leaf_type = bplus_leaf<Key, Value, Order, LeafSize>;

        union element
        {
            leaf_type* leaf;
            bplus_node* node;
        };


        bplus_node* parent{};

        struct {
            size_t parentIndex : ((sizeof(size_t)*8)-1);
            bool hasLeaves : 1;
        } _data{};

        std::array<key_type, Order> keys{};
        std::array<element, Order + 1> nodes{};

        bplus_node(bplus_node* parent, size_t parentIndex, bool hasLeaves)
            : parent(parent)
        {
            _data.hasLeaves = hasLeaves;
            _data.parentIndex = parentIndex;
        }

    public:
        bplus_node(const bplus_node&) = delete;
        bplus_node(bplus_node&&) = default;
        bplus_node& operator =(const bplus_node&) = delete;
        bplus_node& operator =(bplus_node&&) = default;

        ~bplus_node()
        {
            if (_data.hasLeaves)
            {
                for (auto& elem : nodes) {
                    if (elem.leaf != nullptr) delete elem.leaf;
                }
            }
            else
            {
                for (auto& elem : nodes) {
                    if (elem.node != nullptr) delete elem.node;
                }
            }
        }

    private:
        leaf_type* prev_leaf() noexcept
        {
            if (parent == nullptr) return nullptr;
            if (_data.parentIndex == 0) return parent->prev_leaf();

            bplus_node* node = parent->nodes[_data.parentIndex - 1].node;
            while (true)
            {
                if (node->_data.hasLeaves)
                {
                    for (size_t i = Order; i != 0; i--)
                    {
                        if (node->nodes[i-1].leaf != nullptr) {
                            return node->nodes[i-1].leaf;
                        }
                    }
                }
                else
                {
                    for (size_t i = Order; i != 0; i--)
                    {
                        if (node->nodes[i-1].node != nullptr) {
                            node = node->nodes[i].node; break;
                        }
                    }
                }
            }
            return nullptr;
        }

        leaf_type& insert_leaf(size_t index, leaf_type*& first, leaf_type*& last)
        {
            // Get left and right leaves
            leaf_type* right;
            leaf_type* left = index == 0 ? prev_leaf() : nodes[index - 1].leaf;

            if (left != nullptr) {
                right = left->rightLeaf;
            }
            else right = first;

            // Create leaf
            auto* leaf = new leaf_type(this, left, right);

            // Update right and left in linked list
            if (left != nullptr) {
                left->rightLeaf = leaf;
            }
            if (right != nullptr) {
                right->leftLeaf = leaf;
            }

            // Update start and end of leaf list
            if (first == nullptr || first->leftLeaf != nullptr) {
                first = leaf;
            }
            if (last == nullptr || last->rightLeaf != nullptr) {
                last = leaf;
            }
            return *leaf;
        }

        size_t add(Key key, Value value, leaf_type*& first, leaf_type*& last)
        {
            size_t i = 0;
            for (; i < Order; i++) {
                if (nodes[i+1].node == nullptr || key < keys[i]) break;
            }

            if (_data.hasLeaves)
            {
                if (nodes[i].leaf == nullptr) {
                    nodes[i].leaf = &insert_leaf(i, first, last);
                }
                return nodes[i].leaf->add(std::move(key), std::move(value));
            }
            else
            {
                if (nodes[i].node == nullptr) {
                    nodes[i].node = new bplus_node(this, i, true);
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
        using pointer = const value_type*;
        using reference = const value_type&;

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
            if (++index == leaf->count && leaf->rightLeaf != nullptr)
            {
                leaf = leaf->rightLeaf;
                index = 0;
            }
            return *this;
        }

        leaf_iterator operator ++(int) noexcept
        {
            leaf_iterator out(leaf, index);
            operator++();
            return out;
        }

        leaf_iterator& operator --() noexcept
        {
            if (index-- == 0)
            {
                leaf = leaf->leftLeaf;
                index = leaf->count - 1;
            }
            return *this;
        }

        leaf_iterator operator --(int) noexcept
        {
            leaf_iterator out(leaf, index);
            operator--();
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

        template<typename Key, typename Value, size_t Order, size_t LeafSize>
        friend class bplus_tree;

    public:
        leaf_iterable(Leaf* first, size_t start, Leaf* last, size_t end)
            : first(first), start(start), last(last), _end(end) { }

        leaf_iterator<Leaf> begin() noexcept {
            return leaf_iterator<Leaf>(first, start);
        }
        leaf_iterator<Leaf> end() noexcept {
            return leaf_iterator<Leaf>(last, _end);
        }
        auto rbegin() noexcept {
            return std::reverse_iterator<leaf_iterator<Leaf>>(end());
        }
        auto rend() noexcept {
            return std::reverse_iterator<leaf_iterator<Leaf>>(begin());
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
        node_type root{nullptr, 0, true};

        leaf_type* firstLeaf;
        leaf_type* lastLeaf;

    public:
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

        void add(Key key, Value value) &
        {
            _height = root.add(std::move(key), std::move(value),
                firstLeaf, lastLeaf);
            _size++;
        }

        auto search_range(range_start = range_start{}, range_end = range_end{},
            bool = true, bool = true) const &
        {
            return leaf_iterable<const leaf_type>(firstLeaf, 0,
                lastLeaf, lastLeaf != nullptr ? lastLeaf->count : 0);
        }

        template<typename T>
        auto search_range(const T& start, range_end = range_end{},
            bool inclusiveStart = true, bool = true) const &
        {
            static_assert(!std::is_same<T, range_end>::value,"");
            auto iter = search_range(range_start{}, range_end{});

            // Get first leaf and index of first item
            size_t firstIndex = 0;
            const leaf_type* firstLeaf = find_leaf(start, inclusiveStart);

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
            return leaf_iterable<const leaf_type>(
                firstLeaf, firstIndex, iter.last, iter._end);
        }

        template<typename T>
        auto search_range(range_start, const T& end, bool = true,
            bool inclusiveEnd = true) const &
        {
            static_assert(!std::is_same<T, range_start>::value,"");
            auto iter = search_range(range_start{}, range_end{});

            // Get last leaf and index of one past the final item
            size_t lastIndex = 0;
            const leaf_type* lastLeaf = find_leaf(end, inclusiveEnd);

            if (lastLeaf != nullptr && lastLeaf->count != 0)
            {
                lastIndex = lastLeaf->count;
                for (; lastIndex != 0; lastIndex--)
                {
                    if ((inclusiveEnd && lastLeaf->items[lastIndex-1].first == end) ||
                        lastLeaf->items[lastIndex-1].first < end) break;
                }
                if (lastIndex == lastLeaf->count && lastLeaf->rightLeaf != nullptr)
                {
                    lastLeaf = lastLeaf->rightLeaf;
                    lastIndex = 0;
                }
            }
            return leaf_iterable<const leaf_type>(
                iter.first, iter.start, lastLeaf, lastIndex);
        }

        template<typename T, typename Y>
        auto search_range(const T& start, const Y& end,
            bool inclusiveStart = true, bool inclusiveEnd = true) const &
        {
            auto leftIter = search_range(start, range_end{}, inclusiveStart);
            auto rightIter = search_range(range_start{}, end, true, inclusiveEnd);

            return leaf_iterable<const leaf_type>(
                leftIter.first, leftIter.start, rightIter.last, rightIter._end);
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
                if (node->_data.hasLeaves) return node->nodes[i].leaf;
                else node = node->nodes[i].node;
            }
            return nullptr;
        }
    };
}
