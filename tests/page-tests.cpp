/* page-tests.cpp - (c) 2018 James Renwick */
#include "ostest/ostest.hpp"
#include <pages.hpp>

using namespace osdb;

using pid_type = uint8_t;
using size_type = size_t;

TEST_SUITE(PageSuite);

TEST(PageSuite, NewManagerTest)
{
    auto mgr = make_page_manager<pid_type, size_type>(1, 1,
        [&](pid_type, uint8_t*, size_type) {
            return error::None;
        },
        [&](pid_type, const uint8_t*, size_type) {
            return error::None;
        },
        [&](size_type) {
            return unexpected<error>(error::Some);
        },
        [&](pid_type, size_type) {
            return error::None;
        }
    );
}

TEST(PageSuite, PageAllocWriteCallbackTest)
{
    constexpr const pid_type pid = 7;
    constexpr const size_type pageSize = 128;

    size_t readCallbackCount = 0;

    size_t writeCallbackCount = 0;
    pid_type write_page;
    const uint8_t* write_data;
    size_t write_pageSize;

    size_t allocCallbackCount = 0;
    size_t alloc_size;

    size_t freeCallbackCount = 0;

    auto mgrEx = make_page_manager<pid_type, size_type>(3, pageSize,
        [&](pid_type, uint8_t*, size_type)
        {
            readCallbackCount++;
            return error::None;
        },
        [&](pid_type page, const uint8_t* data, size_type pageSize)
        {
            writeCallbackCount++;
            write_page = page;
            write_data = data;
            write_pageSize = pageSize;
            return error::None;
        },
        [&](size_type size)
        {
            allocCallbackCount++;
            alloc_size = size;
            return pid;
        },
        [&](pid_type, size_type) {
            freeCallbackCount++;
            return error::None;
        }
    );
    ASSERT(mgrEx.operator bool());

    auto& mgr = mgrEx.value();
    EXPECT_ZERO(allocCallbackCount);

    pid_type pageID;
    {
        auto page = mgr.new_pinned_page();
        ASSERT(page.operator bool());
        pageID = page.value().id();

        EXPECT_EQ(pageID, pid);
        EXPECT_ZERO(readCallbackCount);
        EXPECT_ZERO(writeCallbackCount);
        EXPECT_EQ(allocCallbackCount, 1);
        EXPECT_EQ(alloc_size, pageSize);
        EXPECT(page.value().dirty());
    }

    auto e = mgr.flush_page(pageID);
    EXPECT_EQ(e, error::None);

    EXPECT_EQ(readCallbackCount, 0);
    EXPECT_EQ(writeCallbackCount, 1);

    EXPECT_EQ(write_page, pid);
    EXPECT_NEQ(write_data, nullptr);
    EXPECT_EQ(write_pageSize, pageSize);

    EXPECT_EQ(allocCallbackCount, 1);
    EXPECT_ZERO(freeCallbackCount);
}

TEST(PageSuite, FlushNone)
{
    uint8_t zero{};

    auto mgrEx = make_page_manager<pid_type, size_type>(1, 128,
        [&](pid_type, uint8_t*, size_type) {
            zero++; return error::None;
        },
        [&](pid_type, const uint8_t*, size_type) {
            zero++; return error::None;
        },
        [&](size_type) {
            zero++; return unexpected<error>(error::Some);
        },
        [&](pid_type, size_type) {
            zero++; return error::None;
        }
    );
    ASSERT(mgrEx.operator bool());
    auto& mgr = mgrEx.value();

    mgr.flush_free_pages();
    EXPECT_NEQ(mgr.flush_page(0), error::None);
    EXPECT_NEQ(mgr.flush_page(1), error::None);
    EXPECT_ZERO(zero);
}

TEST(PageSuite, SinglePageReadWrite)
{
    constexpr size_type pageSize = 128;
    constexpr size_type pageid = 7;

    const uint8_t pageData[pageSize] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x99 };
    uint8_t output[pageSize]{};

    auto mgrEx = make_page_manager<pid_type, size_type>(1, pageSize,
        [&](pid_type, uint8_t* ptr, size_type)
        {
            std::memcpy(ptr, pageData, pageSize);
            return error::None;
        },
        [&](pid_type, const uint8_t* data, size_type)
        {
            std::memcpy(output, data, pageSize);
            return error::None;
        },
        [&](size_type) {
            return unexpected<error>(error::Some);
        },
        [&](pid_type, size_type) {
            return error::None;
        }
    );
    ASSERT(mgrEx.operator bool());
    auto& mgr = mgrEx.value();

    {
        // Pin page
        auto pg = mgr.pin_page(pageid);
        ASSERT(pg.operator bool());

        EXPECT_EQ(pg.value().id(), pageid);
        EXPECT_EQ(pg.value().size(), pageSize);
        EXPECT(!pg.value().dirty());

        // Load page data
        EXPECT_ZERO((std::memcmp(pg.value().data(), pageData, pageSize)));

        // Change final byte and mark as dirty
        pg.value().data()[pageSize - 1] = 0xFF;
        pg.value().mark_dirty();
        EXPECT(pg.value().dirty());
    }

    // Unpin and write page data, compare output
    EXPECT_EQ(mgr.flush_page(pageid), error::None);
    EXPECT_ZERO((std::memcmp(pageData, output, pageSize - 1)));
    EXPECT_EQ(output[pageSize - 1], 0xFF);
}

TEST(PageSuite, PoolFull)
{
    constexpr size_type poolSize = 1;
    constexpr size_type pageSize = 128;

    auto mgrEx = make_page_manager<pid_type, size_type>(poolSize, pageSize,
        [&](pid_type, uint8_t*, size_type) {
            return error::None;
        },
        [&](pid_type, const uint8_t*, size_type) {
            return error::None;
        },
        [&](size_type) {
            return unexpected<error>(error::Some);
        },
        [&](pid_type, size_type) {
            return error::None;
        }
    );
    ASSERT(mgrEx.operator bool());
    auto mgr = std::move(mgrEx.value());

    {
        auto e1 = mgr.pin_page(1);
        EXPECT(e1.operator bool());
        decltype(mgr)::pinned_page p1{std::move(e1.value())};

        auto e2 = mgr.pin_page(1);
        EXPECT(e2.operator bool());
        decltype(mgr)::pinned_page p2{std::move(e2.value())};

        p1.mark_dirty();
        ASSERT(p1.dirty());

        auto p3 = mgr.pin_page(2);
        EXPECT(!p3.operator bool());
    }
}

TEST(PageSuite, AddSingleRecord)
{
    constexpr size_type poolSize = 1;
    constexpr size_type pageSize = 256;

    const uint8_t data[] = {
        0x45, 0x56, 0x67, 0x78, 0x89
    };
    constexpr size_t dataSize = sizeof(data);

    uint8_t pageData[pageSize];

    auto mgrEx = make_page_manager<pid_type, size_type>(poolSize, pageSize,
        [&](pid_type, uint8_t* d, size_type s) {
            std::memcpy(d, pageData, s);
            return error::None;
        },
        [&](pid_type, const uint8_t* d, size_type s) {
            std::memcpy(pageData, d, s);
            return error::None;
        },
        [&, done=false](size_type)
            mutable -> expected<pid_type, error>
        {
            if (done) return unexpected<error>(error::Some);
            else { done = true; return 1; }
        },
        [&](pid_type, size_type) {
            return error::None;
        }
    );
    ASSERT(mgrEx.operator bool());
    auto& mgr = mgrEx.value();

    auto pageEx = mgr.new_pinned_page();
    ASSERT(pageEx.operator bool());

    auto recordEx = osdb::add_record(mgr, pageEx.value().id(), data, dataSize);
    ASSERT(recordEx.operator bool());
    record_index<pid_type, size_type> index = recordEx.value();

    EXPECT_EQ(index.offset, 0);
    EXPECT_EQ(index.pageid, pageEx.value().id());
    EXPECT_EQ(index.size, dataSize);
    EXPECT_EQ(index.slot_index, 0);
    EXPECT(pageEx.value().dirty());
}

TEST(PageSuite, AddReadSingleRecord)
{
    constexpr size_type poolSize = 1;
    constexpr size_type pageSize = 256;

    const uint8_t data[] = {
        0x45, 0x56, 0x67, 0x78, 0x89
    };
    constexpr size_t dataSize = sizeof(data);

    uint8_t pageData[pageSize];
    uint8_t dataOut[dataSize]{};

    auto mgrEx = make_page_manager<pid_type, size_type>(poolSize, pageSize,
        [&](pid_type, uint8_t* d, size_type s) {
            std::memcpy(d, pageData, s);
            return error::None;
        },
        [&](pid_type, const uint8_t* d, size_type s) {
            std::memcpy(pageData, d, s);
            return error::None;
        },
        [&, done=false](size_type)
            mutable -> expected<pid_type, error>
        {
            if (done) return unexpected<error>(error::Some);
            else { done = true; return 1; }
        },
        [&](pid_type, size_type) {
            return error::None;
        }
    );
    ASSERT(mgrEx.operator bool());
    auto& mgr = mgrEx.value();

    auto pageEx = mgr.new_pinned_page();
    ASSERT(pageEx.operator bool());

    auto recordEx = osdb::add_record(mgr, pageEx.value().id(), data, dataSize);
    ASSERT(recordEx.operator bool());
    record_index<pid_type, size_type> index = recordEx.value();

    auto recordPageEx = mgr.pin_page(index.pageid);
    ASSERT(recordPageEx.operator bool());

    auto e1 = osdb::read_record(recordPageEx.value(), index, dataOut, dataSize);
    ASSERT_EQ(e1, error::None);
    EXPECT_ZERO((std::memcmp(data, dataOut, dataSize)));

    std::memset(dataOut, 0, dataSize);

    auto e2 = osdb::read_record(recordPageEx.value(), index.slot_index,
        dataOut, dataSize);
    ASSERT(e2.operator bool());

    EXPECT_EQ(e2.value().offset, index.offset);
    EXPECT_EQ(e2.value().pageid, index.pageid);
    EXPECT_EQ(e2.value().size, index.size);
    EXPECT_EQ(e2.value().slot_index, index.slot_index);
    EXPECT_ZERO((std::memcmp(data, dataOut, dataSize)));
}

TEST(PageSuite, AddReadTwoRecords)
{
    constexpr size_type poolSize = 1;
    constexpr size_type pageSize = 256;

    const uint8_t data1[] = {
        0x45, 0x56, 0x67, 0x78, 0x89
    };
    const uint8_t data2[] = {
        0x14, 0x82, 0x22, 0x91, 0x03
    };
    constexpr size_t dataSize = sizeof(data1);
    static_assert(sizeof(data1) == sizeof(data2),"");

    uint8_t pageData[pageSize * 2];
    uint8_t dataOut1[dataSize]{};
    uint8_t dataOut2[dataSize]{};

    auto mgrEx = make_page_manager<pid_type, size_type>(poolSize, pageSize,
        [&](pid_type p, uint8_t* d, size_type s) {
            std::memcpy(d, pageData + (pageSize * (p-1)), s);
            return error::None;
        },
        [&](pid_type p, const uint8_t* d, size_type s) {
            std::memcpy(pageData + (pageSize * (p-1)), d, s);
            return error::None;
        },
        [&, i=0](size_type)
            mutable -> expected<pid_type, error>
        {
            if (i == 2) return unexpected<error>(error::Some);
            else { return ++i; }
        },
        [&](pid_type, size_type) {
            return error::None;
        }
    );
    ASSERT(mgrEx.operator bool());
    auto& mgr = mgrEx.value();

    auto pageEx = mgr.new_pinned_page();
    ASSERT(pageEx.operator bool());

    auto record1Ex = osdb::add_record(mgr, pageEx.value().id(), data1, dataSize);
    ASSERT(record1Ex.operator bool());
    record_index<pid_type, size_type> index1 = record1Ex.value();

    auto record2Ex = osdb::add_record(mgr, pageEx.value().id(), data2, dataSize);
    ASSERT(record2Ex.operator bool());
    record_index<pid_type, size_type> index2 = record2Ex.value();

    EXPECT_EQ(index1.pageid, index2.pageid);
    EXPECT_LT(index1.offset, index2.offset);
    EXPECT_EQ(index1.size, dataSize);
    EXPECT_EQ(index2.size, dataSize);
    EXPECT_EQ(index1.slot_index, 0);
    EXPECT_EQ(index2.slot_index, 1);
    EXPECT_EQ(index1.offset, 0);
    EXPECT_EQ(index2.offset, dataSize);

    auto recordPageEx1 = mgr.pin_page(index1.pageid);
    ASSERT(recordPageEx1.operator bool());
    auto recordPageEx2 = mgr.pin_page(index2.pageid);
    ASSERT(recordPageEx2.operator bool());

    auto e1 = osdb::read_record(recordPageEx1.value(), index1, dataOut1, dataSize);
    ASSERT_EQ(e1, error::None);
    EXPECT_ZERO((std::memcmp(data1, dataOut1, dataSize)));

    auto e2 = osdb::read_record(recordPageEx2.value(), index2, dataOut2, dataSize);
    ASSERT_EQ(e2, error::None);
    EXPECT_ZERO((std::memcmp(data2, dataOut2, dataSize)));

    std::memset(dataOut1, 0, dataSize);
    std::memset(dataOut2, 0, dataSize);

    auto e3 = osdb::read_record(recordPageEx1.value(), index1.slot_index,
        dataOut1, dataSize);
    ASSERT(e3.operator bool());

    auto e4 = osdb::read_record(recordPageEx2.value(), index2.slot_index,
        dataOut2, dataSize);
    ASSERT(e4.operator bool());

    EXPECT_EQ(e3.value().offset, index1.offset);
    EXPECT_EQ(e3.value().pageid, index1.pageid);
    EXPECT_EQ(e3.value().size, index1.size);
    EXPECT_EQ(e3.value().slot_index, index1.slot_index);
    EXPECT_ZERO((std::memcmp(data1, dataOut1, dataSize)));

    EXPECT_EQ(e4.value().offset, index2.offset);
    EXPECT_EQ(e4.value().pageid, index2.pageid);
    EXPECT_EQ(e4.value().size, index2.size);
    EXPECT_EQ(e4.value().slot_index, index2.slot_index);
    EXPECT_ZERO((std::memcmp(data2, dataOut2, dataSize)));
}

TEST(PageSuite, AddReadSpanningRecords)
{
    const uint8_t data1[] = {
        0x45, 0x56, 0x67, 0x78, 0x89
    };

    constexpr size_type poolSize = 2;
    constexpr size_type pageSize = sizeof(page_footer<pid_type, size_type>)
        + sizeof(size_type) + sizeof(data1);

    const uint8_t data2[] = {
        0x14, 0x82, 0x22, 0x91, 0x03
    };
    constexpr size_t dataSize = sizeof(data1);
    static_assert(sizeof(data1) == sizeof(data2),"");

    uint8_t pageData[pageSize * 2];
    uint8_t dataOut1[dataSize]{};
    uint8_t dataOut2[dataSize]{};

    auto mgrEx = make_page_manager<pid_type, size_type>(poolSize, pageSize,
        [&](pid_type p, uint8_t* d, size_type s) {
            std::memcpy(d, pageData + (pageSize * (p-1)), s);
            return error::None;
        },
        [&](pid_type p, const uint8_t* d, size_type s) {
            std::memcpy(pageData + (pageSize * (p-1)), d, s);
            return error::None;
        },
        [&, i=0](size_type)
            mutable -> expected<pid_type, error>
        {
            if (i == 2) return unexpected<error>(error::Some);
            else { return ++i; }
        },
        [&](pid_type, size_type) {
            return error::None;
        }
    );
    ASSERT(mgrEx.operator bool());
    auto& mgr = mgrEx.value();

    pid_type pid;
    {
        auto pageEx = mgr.new_pinned_page();
        ASSERT(pageEx.operator bool());
        pid = pageEx.value().id();
    }
    auto record1Ex = osdb::add_record(mgr, pid, data1, dataSize);
    ASSERT(record1Ex.operator bool());
    record_index<pid_type, size_type> index1 = record1Ex.value();

    auto record2Ex = osdb::add_record(mgr, pid, data2, dataSize);
    ASSERT(record2Ex.operator bool());
    record_index<pid_type, size_type> index2 = record2Ex.value();

    EXPECT_NEQ(index1.pageid, index2.pageid);
    EXPECT_EQ(index1.offset, index2.offset);
    EXPECT_EQ(index1.size, dataSize);
    EXPECT_EQ(index2.size, dataSize);
    EXPECT_EQ(index1.slot_index, 0);
    EXPECT_EQ(index2.slot_index, 0);
    EXPECT_EQ(index1.offset, 0);
    EXPECT_EQ(index2.offset, 0);

    auto recordPageEx1 = mgr.pin_page(index1.pageid);
    ASSERT(recordPageEx1.operator bool());
    auto recordPageEx2 = mgr.pin_page(index2.pageid);
    ASSERT(recordPageEx2.operator bool());

    auto e1 = osdb::read_record(recordPageEx1.value(), index1, dataOut1, dataSize);
    ASSERT_EQ(e1, error::None);
    EXPECT_ZERO((std::memcmp(data1, dataOut1, dataSize)));

    auto e2 = osdb::read_record(recordPageEx2.value(), index2, dataOut2, dataSize);
    ASSERT_EQ(e2, error::None);
    EXPECT_ZERO((std::memcmp(data2, dataOut2, dataSize)));

    std::memset(dataOut1, 0, dataSize);
    std::memset(dataOut2, 0, dataSize);

    auto e3 = osdb::read_record(recordPageEx1.value(), index1.slot_index,
        dataOut1, dataSize);
    ASSERT(e3.operator bool());

    auto e4 = osdb::read_record(recordPageEx2.value(), index2.slot_index,
        dataOut2, dataSize);
    ASSERT(e4.operator bool());

    EXPECT_EQ(e3.value().offset, index1.offset);
    EXPECT_EQ(e3.value().pageid, index1.pageid);
    EXPECT_EQ(e3.value().size, index1.size);
    EXPECT_EQ(e3.value().slot_index, index1.slot_index);
    EXPECT_ZERO((std::memcmp(data1, dataOut1, dataSize)));

    EXPECT_EQ(e4.value().offset, index2.offset);
    EXPECT_EQ(e4.value().pageid, index2.pageid);
    EXPECT_EQ(e4.value().size, index2.size);
    EXPECT_EQ(e4.value().slot_index, index2.slot_index);
    EXPECT_ZERO((std::memcmp(data2, dataOut2, dataSize)));
}

TEST(PageSuite, AddLargeRecord)
{
    const uint8_t data[] = {
        0x45, 0x56, 0x67, 0x78, 0x89
    };
    constexpr size_t dataSize = sizeof(data);

    constexpr size_type poolSize = 1;
    constexpr size_type pageSize = sizeof(page_footer<pid_type, size_type>)
        + sizeof(size_type) + dataSize - 1;

    uint8_t pageData[pageSize];

    auto mgrEx = make_page_manager<pid_type, size_type>(poolSize, pageSize,
        [&](pid_type, uint8_t* d, size_type s) {
            std::memcpy(d, pageData, s);
            return error::None;
        },
        [&](pid_type, const uint8_t* d, size_type s) {
            std::memcpy(pageData, d, s);
            return error::None;
        },
        [&, done=false](size_type)
            mutable -> expected<pid_type, error>
        {
            if (done) return unexpected<error>(error::Some);
            else { done = true; return 1; }
        },
        [&](pid_type, size_type) {
            return error::None;
        }
    );
    ASSERT(mgrEx.operator bool());
    auto& mgr = mgrEx.value();

    auto pageEx = mgr.new_pinned_page();
    ASSERT(pageEx.operator bool());

    auto recordEx = osdb::add_record(mgr, pageEx.value().id(), data, dataSize);
    EXPECT(!recordEx.operator bool());
}
