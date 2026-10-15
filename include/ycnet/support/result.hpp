#pragma once
#include <concepts>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

template <class container_type>
concept container = requires(container_type a, const container_type b)
{
	requires std::regular<container_type>;
	requires std::swappable<container_type>;
	requires std::destructible<typename container_type::value_type>;
	requires std::same_as<typename container_type::reference, typename container_type::value_type&>;
	requires std::same_as<typename container_type::const_reference, const typename container_type::value_type&>;
	requires std::forward_iterator<typename container_type::iterator>;
	requires std::forward_iterator<typename container_type::const_iterator>;
	requires std::signed_integral<typename container_type::difference_type>;
	requires std::same_as<typename container_type::difference_type, typename std::iterator_traits<typename container_type::iterator>::difference_type>;
	requires std::same_as<typename container_type::difference_type, typename std::iterator_traits<typename container_type::const_iterator>::difference_type>;
	{ a.begin() }	-> std::convertible_to<typename container_type::iterator>;
	{ a.end() }		-> std::convertible_to<typename container_type::iterator>;
	{ b.begin() }	-> std::convertible_to<typename container_type::const_iterator>;
	{ b.end() }		-> std::convertible_to<typename container_type::const_iterator>;
	{ a.cbegin() }	-> std::convertible_to<typename container_type::const_iterator>;
	{ a.cend() }	-> std::convertible_to<typename container_type::const_iterator>;
	{ a.size() }	-> std::convertible_to<typename container_type::size_type>;
	{ a.max_size() }-> std::convertible_to<typename container_type::size_type>;
	{ a.empty() }	-> std::convertible_to<bool>;
};

namespace ycnet::detail {
	template <typename value_type>
	struct result : public std::optional<value_type> {
		std::string err;
		
		result() {}
		explicit result(value_type& value) { *static_cast<std::optional<value_type>*>(this) = value; }
		explicit result(value_type&& value) { *static_cast<std::optional<value_type>*>(this) = std::move(value); }
		result(result<value_type>& other) { operator=(other); }
		result(result<value_type>&& other) noexcept { operator=(other); }
		
		result(const char* e) { err = e;}
		result(const std::string e) { err = e; }
		result(const std::string&& e) { err = e; }
		result& operator= (const std::string&& e) { err = e; return *this; }
		result& operator= (const char* e) { err = e; return *this; }
		
		result& operator= (result<value_type>& other) {
			if (other.has_value()) *static_cast<std::optional<value_type>*>(this) = other.value();
			else													 err = other.err;
			return *this;
		}
		result& operator= (result<value_type>&& other) noexcept {
			if (other.has_value()) *static_cast<std::optional<value_type>*>(this) = other.value();
			else													 err = other.err;
			return *this;
		}
		result& operator= (value_type& value) {
			*static_cast<std::optional<value_type>*>(this) = value;
			return *this;
		}
		result& operator= (value_type&& value) {
			*static_cast<std::optional<value_type>*>(this) = std::move(value);
			return *this;
		}

		auto operator| (auto&& function) -> decltype(function(this->value())) {
			if (this->has_value()) return function (this->value());
			return err;
		}
		
		auto operator| (auto&& function) -> decltype(function()) {
			if (this->has_value()) return function();
			return err;
		}

	};
}
