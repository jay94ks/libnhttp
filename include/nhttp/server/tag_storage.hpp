#pragma once

#include <any>
#include <typeindex>
#include <unordered_map>

namespace nhttp::server {

	/**
	 * class tag_storage.
	 * type-erased, per-type slot storage. this is what lets any extension attach
	 * its own strongly-typed piece of state to a connection or a request without
	 * a shared context class knowing about it — see CONCEPTS.md §1's tag system.
	 * two independent instances exist per exchange: one on the connection
	 * (survives across keep-alive requests) and one on the request (reset every
	 * request) — see server::connection / server::request.
	 */
	class tag_storage {
	public:
		/* gets the existing T, or default/args-constructs one if absent. */
		template<typename T, typename... Args>
		T& ensure(Args&&... args) {
			const std::type_index key(typeid(T));
			auto it = slots_.find(key);

			if (it == slots_.end())
				it = slots_.emplace(key, std::make_any<T>(std::forward<Args>(args)...)).first;

			return std::any_cast<T&>(it->second);
		}

		/* nullptr if T has never been ensure()'d on this storage. */
		template<typename T>
		T* get() noexcept {
			const std::type_index key(typeid(T));
			const auto it = slots_.find(key);

			if (it == slots_.end())
				return nullptr;

			return std::any_cast<T>(&it->second);
		}

		template<typename T>
		bool has() const noexcept {
			return slots_.find(std::type_index(typeid(T))) != slots_.end();
		}

		void clear() { slots_.clear(); }

	private:
		std::unordered_map<std::type_index, std::any> slots_;
	};

}
