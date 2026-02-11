#pragma once
#include <ranges>
#include <tuple>
#include <iterator>

namespace views {

    template <typename BaseIter, typename Sentinel>
    class enumerate_iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = std::pair<std::size_t, typename std::iterator_traits<BaseIter>::value_type>;

        struct result_type {
            std::size_t index;
            decltype(*std::declval<BaseIter>()) value;
        };

        enumerate_iterator() = default;
        enumerate_iterator(BaseIter current, std::size_t index)
            : current(current), index(index) {
        }

        result_type operator*() const {
            return { index, *current };
        }

        enumerate_iterator& operator++() {
            ++current;
            ++index;
            return *this;
        }

        enumerate_iterator operator++(int) {
            auto temp = *this;
            ++(*this);
            return temp;
        }

        bool operator==(const Sentinel& other) const {
            return current == other;
        }

        bool operator==(const enumerate_iterator& other) const {
            return current == other.current;
        }

        bool operator!=(const Sentinel& other) const { return !(*this == other); }
        bool operator!=(const enumerate_iterator& other) const { return !(*this == other); }

    private:
        BaseIter current;
        std::size_t index = 0;
    };

    template <std::ranges::view V>
    class enumerate_view : public std::ranges::view_interface<enumerate_view<V>> {
    public:
        enumerate_view() = default;
        explicit enumerate_view(V base) : base(std::move(base)) {}

        auto begin() {
            return enumerate_iterator<std::ranges::iterator_t<V>, std::ranges::sentinel_t<V>>(
                std::ranges::begin(base), 0
            );
        }

        auto end() {
            return std::ranges::end(base);
        }

    private:
        V base;
    };

    template <typename R>
    enumerate_view(R&&) -> enumerate_view<std::views::all_t<R>>;

    struct enumerate_fn {
        template <std::ranges::viewable_range R>
        auto operator()(R&& r) const {
            return enumerate_view<std::views::all_t<R>>(std::views::all(std::forward<R>(r)));
        }

        template <std::ranges::viewable_range R>
        friend auto operator|(R&& r, const enumerate_fn& fn) {
            return fn(std::forward<R>(r));
        }
    };

    inline constexpr enumerate_fn enumerate;
}
