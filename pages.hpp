/* pages.hpp - (c) 2018 James Renwick */
#include "expected.hpp"
#include <vector>
#include <memory>
#include <algorithm>
#include <cstring>
#include <stddef.h>

namespace osdb
{
    enum class error
    {
        None,
        Some
    };

    template<typename pid_type, typename size_type>
    struct page_footer
    {
        size_type records;
        size_type freeSpace;
        pid_type prev_page;
        pid_type next_page;
    } __attribute__((packed));

    template<typename pid_type, typename size_type>
    struct record_index
    {
        pid_type pageid;
        size_type slot_index;
        size_type offset;
        size_type size;

        bool operator ==(const record_index& other) noexcept {
            return std::memcmp(this, &other, sizeof(record_index)) == 0;
        }
        bool operator !=(const record_index& other) noexcept {
            return !(*this == other);
        }
    };

    template<typename pid_type, typename size_type>
    struct field_index
    {
        pid_type pageid;
        size_type slot_index;
        size_type field_index;
        size_type offset;
        size_type size;
    };

    template<typename T>
    auto read_value(const uint8_t* buffer)
    {
        static_assert(std::is_trivially_copyable<T>::value,"");
        T output;
        std::memcpy(&output, buffer, sizeof(T));
        return output;
    }

    template<typename T>
    void write_value(uint8_t* buffer, const T& value)
    {
        static_assert(std::is_trivially_copyable<T>::value,"");
        std::memcpy(buffer, &value, sizeof(T));
    }

    class page_pool
    {
        size_t pageSize{};
        size_t pageCount{};
        std::unique_ptr<uint8_t[]> data{};
        std::unique_ptr<uint8_t[]> freePages{};
    };


    template<typename pid_type, typename size_type,
        typename page_interface>
    class page_manager;


    template<typename pid_type, typename size_type,
        typename page_intf>
    struct pinned_page
    {
        friend page_manager<pid_type, size_type, page_intf>;

    private:
        using manager = page_manager<pid_type, size_type, page_intf>;

        manager* mgr;
        pid_type _pageID{};
        uint8_t* _data{};
        size_type _size{};
        bool _dirty{};

    private:
        pinned_page(manager& mgr, pid_type pageID, uint8_t* data, size_type size) noexcept
            : mgr(&mgr), _pageID(pageID), _data(data), _size(size) { }

    public:
        ~pinned_page();

        pinned_page(const pinned_page&) = delete;
        pinned_page& operator=(const pinned_page&) = delete;

        pinned_page& operator=(pinned_page&& other)
        {
            std::swap(mgr, other.mgr);
            std::swap(_pageID, other._pageID);
            std::swap(_data, other._data);
            std::swap(_size, other._size);
            std::swap(_dirty, other._dirty);
            return *this;
        }

        pinned_page(pinned_page&& other) noexcept
            : mgr(other.mgr), _pageID(other._pageID), _data(other._data),
              _size(other._size), _dirty(other._dirty)
        {
            other._pageID = 0;
            other._dirty = false;
        };

        constexpr pid_type id() noexcept {
            return _pageID;
        }
        constexpr uint8_t* data() noexcept {
            return _data;
        }
        constexpr size_t size() noexcept {
            return _size;
        }
        constexpr bool dirty() noexcept {
            return _dirty;
        }
        constexpr void mark_dirty() noexcept {
            _dirty = true;
        }
    };

    template<typename pid_type, typename size_type, typename F>
    struct is_valid_read_page
    {
        template<typename T, class Rtn = decltype(std::declval<T>()(
            std::declval<pid_type>(), std::declval<uint8_t*>(),
            std::declval<size_type>())),
            class = std::enable_if_t<std::is_same<Rtn, error>::value>>
        static std::true_type test(int);
        template<typename>
        static std::false_type test(...);

        static constexpr bool value = decltype(test<F>(0))::value;
    };

    template<typename pid_type, typename size_type, typename F>
    struct is_valid_write_page
    {
        template<typename T, class Rtn = decltype(std::declval<T>()(
            std::declval<pid_type>(), std::declval<const uint8_t*>(),
            std::declval<size_type>())),
            class = std::enable_if_t<std::is_same<Rtn, error>::value>>
        static std::true_type test(int);
        template<typename>
        static std::false_type test(...);

        static constexpr bool value = decltype(test<F>(0))::value;
    };

    template<typename pid_type, typename size_type, typename F>
    struct is_valid_free_page
    {
        template<typename T, class Rtn = decltype(std::declval<T>()(
            std::declval<pid_type>(), std::declval<size_type>())),
            class = std::enable_if_t<std::is_same<Rtn, error>::value>>
        static std::true_type test(int);
        template<typename>
        static std::false_type test(...);

        static constexpr bool value = decltype(test<F>(0))::value;
    };

    template<typename pid_type, typename size_type, typename F>
    struct is_valid_alloc_page
    {
        template<typename T, class Rtn = decltype(
            std::declval<T>()(std::declval<size_type>())),
            class = std::enable_if_t<
                std::is_same<Rtn, expected<pid_type, error>>::value ||
                std::is_same<Rtn, unexpected<error>>::value ||
                std::is_same<Rtn, pid_type>::value
            >>
        static std::true_type test(int);
        template<typename>
        static std::false_type test(...);

        static constexpr bool value = decltype(test<F>(0))::value;
    };

    template<typename pid_type, typename size_type,
        typename ReadFunc, typename WriteFunc,
        typename AllocFunc, typename FreeFunc>
    struct page_interface
    {
        ReadFunc read_page;
        WriteFunc write_page;
        AllocFunc alloc_page;
        FreeFunc free_page;

        page_interface(ReadFunc f1, WriteFunc f2, AllocFunc f3, FreeFunc f4)
            : read_page(std::move(f1)), write_page(std::move(f2)),
              alloc_page(std::move(f3)), free_page(std::move(f4))
        {
        }

        page_interface(page_interface&&) = default;

        // Poor man's concepts
        static_assert(is_valid_read_page<pid_type, size_type, ReadFunc>::value,
            "Invalid signature for read_page function");
        static_assert(is_valid_write_page<pid_type, size_type, WriteFunc>::value,
            "Invalid signature for write_page function");
        static_assert(is_valid_alloc_page<pid_type, size_type, AllocFunc>::value,
            "Invalid signature for alloc_page function");
        static_assert(is_valid_free_page<pid_type, size_type, FreeFunc>::value,
            "Invalid signature for free_page function");
    };

    template<typename pid_type, typename size_type,
        typename page_interface>
    class page_manager
    {
    public:
        using footer_t = page_footer<pid_type, size_type>;
        using pinned_t = osdb::pinned_page<pid_type, size_type, page_interface>;
        friend pinned_t;

    private:
        struct directory_entry
        {
            bool dirty{};
            pid_type page{};
            size_type poolIndex{};
            size_t pinCount{};
        };

        size_type pageSize{};
        std::unique_ptr<uint8_t[]> pool{};
        std::vector<directory_entry> directory{};

        page_interface interface;

    public:
        using pinned_page = pinned_t;

        page_manager(size_t poolSize, size_type pageSize, page_interface interface)
            : pageSize(pageSize), pool(new uint8_t[poolSize * pageSize]),
              directory(poolSize), interface(std::move(interface))
        {
            for (size_t i = 0; i < poolSize; i++) {
                directory[i].poolIndex = i;
            }
        }

        page_manager(const page_manager&) = delete;
        page_manager& operator=(const page_manager&) = delete;

        page_manager(page_manager&&) = default;
        page_manager& operator=(page_manager&&) = default;

        ~page_manager()
        {
            // Write-back dirty entries
            for (auto& entry : directory)
            {
                if (entry.dirty) {
                    interface.write_page(entry.page,
                        &pool[pageSize*entry.poolIndex], pageSize);
                }
            }
        }

        size_type page_size() const noexcept {
            return pageSize;
        }
        size_type page_data_size() const noexcept {
            return pageSize - sizeof(footer_t);
        }

        expected<pinned_t, error> pin_page(pid_type page)
        {
            // Search in directory
            for (auto& entry : directory)
            {
                if (entry.page == page) {
                    entry.pinCount++;
                    return pinned_t(*this, page, &pool[pageSize*entry.poolIndex], pageSize);
                }
            }
            // If missing, load page into directory
            auto res = load_page(page);
            if (!res) return unexpected<error>(res.error());

            auto poolIndex = directory[res.value()].poolIndex;
            return pinned_t(*this, page, &pool[pageSize*poolIndex], pageSize);
        }

        error flush_page(pid_type page)
        {
            // Write-back dirty entry
            for (auto& entry : directory)
            {
                if (entry.page == page && entry.pinCount == 0 &&
                    entry.dirty)
                {
                    error e = interface.write_page(entry.page,
                        &pool[pageSize*entry.poolIndex], pageSize);
                    if (e != error::None) entry.dirty = false;
                    return e;
                }
            }
            return error::Some;
        }

        error flush_free_pages()
        {
            // Write-back dirty entries
            for (auto& entry : directory)
            {
                if (entry.pinCount == 0 && entry.dirty)
                {
                    error e = interface.write_page(entry.page,
                        &pool[pageSize*entry.poolIndex], pageSize);
                    if (e != error::None) return e;
                    else entry.dirty = false;
                }
            }
            return error::None;
        }

        expected<pinned_page, error> new_pinned_page()
        {
            // Ensure free entry in directory
            auto r = make_dir_entry();
            if (!r) return std::move(r).forward_error();

            size_t i = r.value();
            auto poolIndex = directory[i].poolIndex;

            // Allocate page
            expected<pid_type, error> ex = interface.alloc_page(pageSize);
            if (!ex) return unexpected<error>(std::move(ex.error()));

            // Set directory entry and zero page data
            directory[i].page = ex.value();
            directory[i].pinCount = 1;
            directory[i].dirty = true;
            std::memset(&pool[pageSize*poolIndex], 0, pageSize);

            // Move entry to end (LIFO)
            auto iter = directory.begin() + i;
            std::rotate(iter, iter + 1, directory.end());

            // Write initial footer
            write_value<footer_t>(&pool[pageSize*(poolIndex+1) - sizeof(footer_t)],
                {0, pageSize - sizeof(footer_t), 0, 0});

            auto pin = pinned_t(*this, ex.value(), &pool[pageSize*poolIndex], pageSize);
            pin.mark_dirty();
            return std::move(pin);
        }

    private:
        void unpin_page(pid_type page, bool dirty)
        {
            for (auto& entry : directory)
            {
                if (entry.page == page)
                {
                    if (dirty) {
                        entry.dirty = true;
                    }
                    if (entry.pinCount != 0) {
                        entry.pinCount--;
                    }
                    break;
                }
            }
        }

        expected<size_t, error> make_dir_entry()
        {
            // Get free page
            size_t i = 0;
            for (; i < directory.size(); i++)
            {
                auto& entry = directory[i];
                if (entry.pinCount == 0)
                {
                    // Write-back if dirty
                    if (entry.dirty)
                    {
                        error e = interface.write_page(entry.page,
                            &pool[pageSize*entry.poolIndex], pageSize);

                        if (e != error::None) {
                            return unexpected<error>(e);
                        }
                        entry.dirty = false;
                    }
                    // Reserve by setting pin count to 1
                    entry.pinCount = 1;
                    break;
                }
            }
            // If no space remaining, return error
            if (i == directory.size()) return unexpected<error>();
            return i;
        }

        expected<size_t, error> load_page(pid_type page)
        {
            // Get directory entry
            auto r = make_dir_entry();
            if (!r) return unexpected<error>(r.error());

            size_t i = r.value();
            directory[i].page = page;
            directory[i].pinCount = 1;

            // Read page data
            error e = interface.read_page(page,
                &pool[pageSize*directory[i].poolIndex], pageSize);

            if (e == error::None)
            {
                // Move entry to end (LIFO)
                auto iter = directory.begin() + i;
                std::rotate(iter, iter + 1, directory.end());
                return i;
            }
            else return unexpected<error>(e);
        }
    };

    template<typename pid_type, typename size_type, typename page_intf>
    pinned_page<pid_type, size_type, page_intf>::~pinned_page()
    {
        if (_pageID != 0) mgr->unpin_page(_pageID, _dirty);
    }

    template<typename pid_type, typename size_type,
        typename F1, typename F2, typename F3, typename F4>
    auto make_page_interface(F1&& f1, F2&& f2, F3&& f3, F4&& f4)
    {
        return page_interface<pid_type, size_type,
            F1, F2, F3, F4>
        {
            std::forward<F1>(f1),
            std::forward<F2>(f2),
            std::forward<F3>(f3),
            std::forward<F4>(f4)
        };
    }

    template<typename pid_type, typename size_type,
        typename ReadFunc, typename WriteFunc,
        typename AllocFunc, typename FreeFunc>
    auto make_page_manager(size_t poolSize, size_type pageSize,
        ReadFunc&& readFunction, WriteFunc&& writeFunction,
        AllocFunc&& allocFunction, FreeFunc&& freeFunction)
    {
        auto intf = make_page_interface<pid_type, size_type>(
            std::forward<ReadFunc>(readFunction),
            std::forward<WriteFunc>(writeFunction),
            std::forward<AllocFunc>(allocFunction),
            std::forward<FreeFunc>(freeFunction));

        using mgr_t = page_manager<pid_type, size_type, decltype(intf)>;

        if (sizeof(typename mgr_t::footer_t) + sizeof(size_type) >= pageSize) {
            return expected<mgr_t, error>(unexpected<error>(error::Some));
        }
        return expected<mgr_t, error>(mgr_t(poolSize, pageSize, std::move(intf)));
    }

    template<typename pid_type, typename size_type, typename page_intf>
    expected<record_index<pid_type, size_type>, error> get_record(
        pinned_page<pid_type, size_type, page_intf>& page, size_type recordIndex)
    {
        auto* footerStart = page.data() + page.size() - sizeof(page_footer<pid_type, size_type>);
        auto pageFooter = read_value<page_footer<pid_type, size_type>>(footerStart);

        if (recordIndex >= pageFooter.records) {
            return unexpected<error>(error::Some);
        }

        size_type offset = 0;
        uint8_t* data = footerStart - sizeof(size_type) * pageFooter.records;
        for (size_type i = 0; i < recordIndex; i++)
        {
            offset += read_value<size_type>(data);
            data += sizeof(size_type);
        }
        size_type size = read_value<size_type>(data);
        return record_index<pid_type, size_type>{page.id(), recordIndex, offset, size};
    }

    template<typename pid_type, typename size_type, size_type fieldCount,
        typename page_intf>
    expected<field_index<pid_type, size_type>, error> get_field(
        pinned_page<pid_type, size_type, page_intf>& page,
        const record_index<pid_type, size_type>& record, size_type index)
    {
        if (index >= fieldCount) return unexpected<error>(error::Some);

        size_type offset = 0;
        auto* data = page.data() + record.offset;
        for (size_type i = 0; i < index && i < fieldCount; i++)
        {
            offset += read_value<size_type>(data);
            data += sizeof(size_type);
        }
        size_type size = read_value<size_type>(data);
        return field_index<pid_type, size_type>{
            page.id(), record.slot_index, index, offset, size};
    }

    template<typename pid_type, typename size_type, typename page_intf>
    expected<record_index<pid_type, size_type>, error> add_record(
        page_manager<pid_type, size_type, page_intf>& mgr,
        pid_type pageid, const uint8_t* data, size_type recordSize)
    {
        // TODO: support recordSize + sizeof(size_type) > pageSize
        if (mgr.page_data_size() - sizeof(size_type) < recordSize) {
            return unexpected<error>(error::Some);
        }

        // Pin initial page
        auto ex = mgr.pin_page(pageid);
        if (!ex) return ex.forward_error();
        auto page = std::move(ex.value());

        while (true)
        {
            auto* footerStart = page.data() + page.size()
                - sizeof(page_footer<pid_type, size_type>);
            auto pageFooter = read_value<page_footer<pid_type, size_type>>(footerStart);

            // Look for a page with enough free space
            if (pageFooter.freeSpace < recordSize + sizeof(size_type))
            {
                // Otherwise allocate new
                if (pageFooter.next_page == 0)
                {
                    auto curPage = std::move(page);
                    auto pageEx = mgr.new_pinned_page();
                    if (!pageEx) return pageEx.forward_error();

                    // Update linked list
                    pageFooter.next_page = pageEx.value().id();
                    write_value<page_footer<pid_type, size_type>>(footerStart, pageFooter);
                    page.mark_dirty();

                    // Move to new page
                    page = std::move(pageEx.value());
                }
                else
                {
                    // Move to next page
                    auto ex = mgr.pin_page(pageFooter.next_page);
                    if (!ex) return ex.forward_error();
                    page = std::move(ex.value());
                }
                pageid = pageFooter.next_page;
                continue;
            }
            else
            {
                // Write record data
                uint8_t* sizeStart = footerStart - sizeof(size_type) * pageFooter.records;
                uint8_t* dataStart = sizeStart - pageFooter.freeSpace;
                std::memcpy(dataStart, data, recordSize);

                // Write record size & update footer
                write_value<size_type>(sizeStart - sizeof(size_type), recordSize);
                pageFooter.records++;
                pageFooter.freeSpace -= recordSize + sizeof(size_type);
                write_value<page_footer<pid_type, size_type>>(footerStart, pageFooter);

                page.mark_dirty();

                return record_index<pid_type, size_type>{
                    pageid, pageFooter.records - 1,
                    static_cast<size_type>(static_cast<std::ptrdiff_t>(
                        dataStart - page.data())),
                    recordSize};
            }
        }
    }

    template<typename pid_type, typename size_type, typename page_intf>
    error read_record(
        pinned_page<pid_type, size_type, page_intf>& page,
        record_index<pid_type, size_type>& record,
        uint8_t* data, size_type bufferSize)
    {
        if (record.pageid != page.id()) {
            return error::Some;
        }
        auto size = std::min(bufferSize, record.size);
        std::memcpy(data, page.data() + record.offset, size);
        return error::None;
    }

    template<typename pid_type, typename size_type, typename page_intf>
    expected<record_index<pid_type, size_type>, error> read_record(
        pinned_page<pid_type, size_type, page_intf>& page,
        size_type& recordIndex, uint8_t* data, size_type bufferSize)
    {
        auto* footerStart = page.data() + page.size()
            - sizeof(page_footer<pid_type, size_type>);
        auto pageFooter = read_value<page_footer<pid_type, size_type>>(footerStart);

        if (recordIndex >= pageFooter.records) {
            return unexpected<error>(error::Some);
        }

        size_type offset = 0;
        const uint8_t* sizeStart = footerStart - sizeof(size_type);
        for (size_type i = 0; i < recordIndex; i++)
        {
            offset += read_value<size_type>(sizeStart);
            sizeStart -= sizeof(size_type);
        }
        size_type size = read_value<size_type>(sizeStart);

        record_index<pid_type, size_type> output{
            page.id(), recordIndex, offset, size};

        size = std::min(size, bufferSize);
        std::memcpy(data, page.data() + offset, size);
        return output;
    }
}
