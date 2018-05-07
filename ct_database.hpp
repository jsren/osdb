/* ct_database.hpp - (c) 2018 James Renwick */
#include <stddef.h>
#include <tuple>
#include <array>
#include <iostream>
#include <string>

template<char ...cs>
struct ct_string 
{
    static std::string to_string()
    {
        char data[] = { cs..., '\0' };
        return std::string(data);
    }

    static constexpr const size_t size = sizeof...(cs);
};

template<typename Char, Char ...C1s, Char ...C2s, size_t ...Is>
constexpr bool are_equal(const ct_string<C1s...>&, const ct_string<C2s...>&,
    std::index_sequence<Is...>) noexcept
{
    return ((C1s == C2s) && ... && true);
};

template<typename Char, Char ...C1s, Char ...C2s>
constexpr bool are_equal(const ct_string<C1s...>& s1, const ct_string<C2s...>& s2) noexcept
{
    if constexpr (sizeof...(C1s) != sizeof...(C2s)) return false;
    else return are_equal(s1, s2, std::make_index_sequence<sizeof...(C1s)>());
};

template<typename Char, Char ...Cs>
auto operator ""_nm()
{
    return ct_string<Cs...>();
}

#define OSDB_STR(x) decltype(x ## _nm)


template<typename Name, typename Type, size_t Width = 0>
struct FieldDefinition
{
    using value_type = Type;
    using name = Name;
    inline static constexpr const size_t width = Width;
};

template<typename FieldDef, size_t Index>
struct Field
{
    using value_type = typename FieldDef::value_type;
    using name_t = typename FieldDef::name;

    inline static constexpr const size_t index = Index;
    inline static constexpr const size_t width = FieldDef::width;

private:
    const std::string _name = name_t::to_string();

public:
    const std::string& name() const noexcept {
        return _name;
    }
};

template<typename Name, typename Field, typename ...Fields>
struct field_for
{
    static constexpr auto get()
    {
        if constexpr (are_equal(Name{}, typename Field::name_t{})) return Field{};
        else if constexpr (sizeof...(Fields) == 0) {
            static_assert(sizeof...(Fields) != 0, "Field does not exist.");
        }
        else return field_for<Name, Fields...>::get();
    }
};

template<typename ...Fields>
struct Table
{
    using field_types = std::tuple<Fields...>;
    using proxy_types = std::tuple<Field<Fields, 0>...>;

    template<typename Name>
    auto operator[](const Name&)
    {
        return field_for<Name, Field<Fields, 0>...>::get();
    }
};


enum class Op
{
    Eq
};

template<typename Lhs, Op op, typename Rhs>
struct FieldOperation
{
    const Lhs& lhs;
    const Rhs& rhs;

    inline static constexpr const Op oper = op;

    FieldOperation(const Lhs& lhs, const Rhs& rhs)
        : lhs(lhs), rhs(rhs) { }
};

template<typename FieldDef, size_t Index, typename T>
auto operator ==(const Field<FieldDef, Index>&, const T& value) noexcept
{
    // Validate operation
    using Field = Field<FieldDef, Index>;
    static_assert(std::is_same_v<decltype(std::declval<typename Field::value_type>() == value), bool>,"");

    return FieldOperation<Field, Op::Eq, decltype(value)>(Field{}, value);
}

using PersonTable = Table<
    FieldDefinition<OSDB_STR("Name"), std::string>,
    FieldDefinition<OSDB_STR("age"), int, 2>>;

using instr_type = int;

template<typename Operation>
constexpr auto optimise() noexcept
{
    std::array<instr_type, 20> output{};

    for (int i = 0; i < 20; i++) {
        output[i] = i * 4;
    }

    return output;
}

template<size_t NSteps, typename Operation>
void execute(const std::array<instr_type, NSteps>& plan, const Operation& op)
{
    for (size_t i = 0; i < NSteps; i++) {
        std::cout << plan[i] << "\n";
    }
}

template<typename ...Tables, typename Func>
void query(Func&& func)
{
    // Get the AST for the operations
    using operation = decltype(std::forward<Func>(func)(Tables{}...));

    // Construct plan for query at compile-time
    constexpr auto plan = optimise<operation>();

    // Execute plan at run-time
    return execute(plan, std::forward<Func>(func)(Tables{}...));
}

int main()
{
    auto result = query<PersonTable, PersonTable>([](auto p1, auto p2) {
        return p1["age"_nm] == p2["age"_nm];
    });
    //.groupByAsc([](auto p1, auto p2) { return p1["age"_nm]; })
    //.project([](auto p1, auto p2) { return p1["age"_nm]; });
}
