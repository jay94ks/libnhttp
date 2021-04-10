#pragma once
#include "../../types.hpp"

namespace nhttp {
namespace server {

	class http_listener;
	class http_context;
	class http_extension;
	class http_extendable_extension;

	using http_extension_ptr = std::shared_ptr<http_extension>;

	/**
	 * class http_extension.
	 * extension base for http-listener.
	 */
	class NHTTP_API http_extension {
		friend class http_listener;
		friend class http_extendable_extension;

	protected:
		uint32_t priority;

	public:
		/**
		 * constructs a new http_extension instance.
		 * @param priority:
		 *	  0x00000000 ~ 0xefffffff : for free use.
		 *    0xf0000000 ~ 0xffffffff : reserved by library.
		 * 
		 * @tips: these priority can be used like (not fixed):
		 *    0x00000000 ~ 0x000fffff : logging system.
		 *    0x00100000 ~ 0x001fffff : error document generator, request content interpreter...
		 *    0x00200000 ~ 0x002fffff : request filters. (e.g. throttle filter ...)
		 *    0x00300000 ~ 0x003fffff : global path mapper. (e.g. directory alias of apache2 ...)
		 *    0x00400000 ~ 0x004fffff : file upload handler, sessions, ...
		 *    0x00500000 ~ 0x005fffff : authorization, permission checker ...
		 *    0x00600000 ~ 0x006fffff : cache driver, proxy driver ...
		 *    0x00700000 ~ 0x007fffff : custom REST api handlers ...
		 */
		http_extension(uint32_t priority = 0x00fffffful) : priority(priority) { }
		virtual ~http_extension() { }

	public:
		inline uint32_t get_priority() const { return priority; }

	protected:
		/* called for collecting extensions. (DO NOT perform much things!) */
		virtual bool on_collect(std::shared_ptr<http_context> context) { return true; }

		/* called for handling a context. */
		virtual bool on_handle(std::shared_ptr<http_context> context) { return true; }
	};

	struct http_extendable_extension_registry;

	/**
	 * class http_extendable_extension.
	 * extendable extension base for http-listener.
	 */
	class NHTTP_API http_extendable_extension : public http_extension {
	public:
		struct extended_t { };

	private:
		static constexpr extended_t by_extended = { };
		std::shared_ptr<http_extendable_extension_registry> registry;

	public:
		http_extendable_extension(uint32_t priority = 0x00fffffful);
		virtual ~http_extendable_extension() { }

	public:
		/* extends this virtual host. */
		void extends(http_extension_ptr extension);

	private:
		virtual bool on_handle(std::shared_ptr<http_context> context) override;

	protected:
		/* called for collecting extensions. (DO NOT perform much things!) */
		virtual bool on_collect(std::shared_ptr<http_context> context) { return true; }

	protected:
		/* called before calling the on_handle method, test if it can be handled. */
		virtual bool on_enter(std::shared_ptr<http_context> context) { return true; }

		/* called after calling the on_handle method, clean states if needed. */
		virtual void on_leave(std::shared_ptr<http_context> context) { }

		/* called for handling a context. */
		virtual bool on_handle(std::shared_ptr<http_context> context, extended_t) { return false; }
	};

}
}