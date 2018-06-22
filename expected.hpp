/* except.hpp - (c) James Renwick */
#include <utility>
#include <string>

namespace osdb
{
    template<typename E>
    struct unexpected
    {
        E value;

        unexpected(E value = E{})
            : value(std::move(value)) { }
    };

    struct error_tag { };


    template<typename T, typename E>
    class expected
    {
        alignas(alignof(T) > alignof(E) ? alignof(T) : alignof(E))
            unsigned char _data[sizeof(T) > sizeof(E) ? sizeof(T) : sizeof(E)];

        bool _has_value{};

    public:
        ~expected() {
            destroy_value();
        }

        expected(unexpected<E> e) noexcept(noexcept(E(std::move(e.value))))
            : _has_value(false)
        {
            new (_data) E(std::move(e.value));
        }

        expected(T value) noexcept(noexcept(T(std::move(value))))
            : _has_value(true)
        {
            new (_data) T(std::move(value));
        }

        expected(const expected& other) :
            _has_value(other._has_value)
        {
            if (other._has_value) {
                new (_data) T(other.value());
            }
            else {
                new (_data) E(other.error());
            }
        }

        expected(expected&& other) : _has_value(other._has_value)
        {
            if (other._has_value) {
                new (_data) T(std::move(other.value()));
            }
            else {
                new (_data) E(std::move(other.error()));
            }
        }

        expected& operator=(const expected& other)
        {
            if (other._has_value)
            {
                if (this->_has_value) {
                    this->value() = other.value();
                }
                else {
                    destroy_value();
                    new (_data) T(other.value());
                }
            }
            else
            {
                if (!this->_has_value) {
                    this->error() = other.error();
                }
                else {
                    destroy_value();
                    new (_data) E(other.error());
                }
            }
            _has_value = other._has_value;
        }

        expected& operator=(expected&& other)
        {
            if (other._has_value)
            {
                if (this->_has_value) {
                    this->value() = std::move(other.value());
                }
                else {
                    destroy_value();
                    new (_data) T(std::move(other.value()));
                }
            }
            else
            {
                if (!this->_has_value) {
                    this->error() = std::move(other.error());
                }
                else {
                    destroy_value();
                    new (_data) E(std::move(other.error()));
                }
            }
            _has_value = other._has_value;
            return *this;
        }

        E& error() noexcept {
            return *reinterpret_cast<E*>(_data);
        }
        const E& error() const noexcept {
            return *reinterpret_cast<const E*>(_data);
        }

        unexpected<E> forward_error() const &
            noexcept(noexcept(E(std::declval<E>())))
        {
            return unexpected<E>(error());
        }

        unexpected<E> forward_error() const &&
            noexcept(noexcept(E(std::move(std::declval<E>()))))
        {
            return unexpected<E>(std::move(error()));
        }

        T& value() noexcept {
            return *reinterpret_cast<T*>(_data);
        }
        const T& value() const noexcept {
            return *reinterpret_cast<T*>(_data);
        }

        operator bool() const noexcept {
            return _has_value;
        }
        bool operator !() const noexcept {
            return !_has_value;
        }

        std::tuple<expected&, bool> tie() {
            return std::tuple<expected&, bool>(*this, _has_value);
        }
        std::tuple<const expected&, bool> tie() const {
            return std::tuple<expected&, bool>(*this, _has_value);
        }

    private:
        void destroy_value()
        {
            if (_has_value) {
                value().~T();
            }
            else error().~E();
        }
    };
}
