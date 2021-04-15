#pragma once
#include "endpoint.hpp"
#include "../hal/event_t.hpp"

namespace nhttp {

	class socket_watcher;
	using tcp = hal::tcp;
	using udp = hal::udp;

	class socket_t {
	public:
		using event_t = hal::event_t;
		friend class socket_watcher;

	protected:
		struct state_t {
			hal::socket_raw_t raw;

			void (*on_event)(socket_t);
			void (*on_dtor )(void* );
			
			struct {
				int8_t io_mode : 1;   /* from watcher, 0: none,  1: watching. */
				int8_t closed : 1;	  /* from watcher, 0: none,  1: closed. */
				int8_t can_read : 1;  /* from watcher, 0: avail, 1: unavail */
				int8_t can_write : 1; /* from watcher, 0: avail, 1: unavail */
			} flags;

			void* data_ptr;
			void* user_tag;

			state_t() : on_event(0), flags({ 0, }), data_ptr(0), user_tag(0) { }
			~state_t() {
				if (user_tag && on_dtor)
					on_dtor(user_tag);

				if (raw.is_valid())
					raw.close();
			}
		};

		std::shared_ptr<state_t> handle;

	public:
		/* create socket instance using addr_type and proto_type. */
		template<typename addr_type = ipv4_addr, typename proto_type = tcp>
		inline static socket_t create() {
			hal::socket_raw_t raw_sock = hal::socket_raw_t::create<addr_type, proto_type>();
			socket_t retval;

			if (raw_sock.is_valid()) {
				retval.handle = std::make_shared<state_t>();
				retval.handle->raw = raw_sock;
			}

			return retval;
		}

	public:
		inline operator bool () const { return !(!handle); }
		inline bool operator !() const { return !handle; }
		inline hal::socket_raw_t get_raw() const { return handle ? handle->raw : hal::socket_raw_t(); }

		inline bool operator ==(const socket_t& other) const { return handle == other.handle; }
		inline bool operator !=(const socket_t& other) const { return handle != other.handle; }

		inline int32_t get_errno() const { return handle ? handle->raw.error() : -1; }
		inline bool is_watching() const { return handle && handle->flags.io_mode; }

		/* this is only working with socket_watcher. */
		inline bool is_alive() const { return handle && !handle->flags.closed; }
		inline bool can_read() const { return handle && handle->flags.can_read; }
		inline bool can_write() const { return handle && handle->flags.can_write; }

		/* get socket tag. */
		inline void* get_tag() const { return handle ? handle->user_tag : nullptr; }

		/* set socket tag. */
		inline void set_tag(void* tag, void(*dtor)(void*)) {
			NHTTP_INIT_ASSERT(handle, "socket isn't initialized!");
			handle->user_tag = tag;
			handle->on_dtor = dtor;
		}

	public:
		/* bind to endpoint. */
		template<typename endpoint_type>
		inline bool bind(const endpoint_type& ep) {
			NHTTP_INIT_ASSERT(handle, "socket isn't initialized!");
			return handle->raw.bind(ep.addr);
		}

		/* connect to endpoint. */
		template<typename endpoint_type>
		inline bool connect(const endpoint_type& ep) {
			NHTTP_INIT_ASSERT(handle, "socket isn't initialized!");
			return handle->raw.connect(ep.addr);
		}

		/* listen the endpoint with backlog. */
		inline bool listen(int32_t backlog = 100) {
			NHTTP_INIT_ASSERT(handle, "socket isn't initialized!");
			return handle->raw.listen(backlog);
		}

		/* accept a connection. */
		inline socket_t accept() {
			NHTTP_INIT_ASSERT(handle, "socket isn't initialized!");
			hal::socket_raw_t sock = handle->raw.accept();
			socket_t retval;

			if (sock.is_valid()) {
				retval.handle = std::make_shared<state_t>();
				retval.handle->raw = sock;
			}

			return retval;
		}

		/* get local address. */
		template<typename addr_type>
		inline bool get_local_addr(addr_type& out_addr) {
			NHTTP_INIT_ASSERT(handle, "socket isn't initialized!");
			return handle->raw.get_local_address(out_addr);
		}

		/* get remote address. */
		template<typename addr_type>
		inline bool get_remote_addr(addr_type& out_addr) {
			NHTTP_INIT_ASSERT(handle, "socket isn't initialized!");
			return handle->raw.get_remote_address(out_addr);
		}

		inline std::string get_local_addr() {
			ipv4_addr v4;

			if (get_local_addr(v4))
				return hal::to_string(v4);

			ipv6_addr v6;
			if (get_local_addr(v6))
				return hal::to_string(v6);

			return "unknown";
		}

		inline std::string get_remote_addr() {
			ipv4_addr v4;

			if (get_remote_addr(v4))
				return hal::to_string(v4);

			ipv6_addr v6;
			if (get_remote_addr(v6))
				return hal::to_string(v6);

			return "unknown";
		}

		/* try read n bytes but, non-blocking. */
		inline ssize_t read(void* buf, size_t n) {
			NHTTP_INIT_ASSERT(handle, "socket isn't initialized!");
			return handle->raw.read(buf, n);
		}

		/* try write n bytes but, non-blocking. */
		inline ssize_t write(const void* buf, size_t n) {
			NHTTP_INIT_ASSERT(handle, "socket isn't initialized!");
			return handle->raw.write(buf, n);
		}

		///* read n bytes explicitly, if under non-blocking, this works same with read() fn. */
		//inline ssize_t read_n(void* buf, size_t n) {
		//	NHTTP_INIT_ASSERT(handle, "socket isn't initialized!");
		//	return handle->raw.read_n(buf, n);
		//}

		///* write n bytes explicitly. if under non-blocking, this works same with read() fn. */
		//inline ssize_t write_n(const void* buf, size_t n) {
		//	NHTTP_INIT_ASSERT(handle, "socket isn't initialized!");
		//	return handle->raw.write_n(buf, n);
		//}

		/* shutdown this socket. */
		inline bool shutdown(int how = SHUT_RDWR) {
			NHTTP_INIT_ASSERT(handle, "socket isn't initialized!");
			return handle->raw.shutdown(how);
		}

		/* close this socket. */
		inline bool close() {
			NHTTP_INIT_ASSERT(handle, "socket isn't initialized!");
			if (handle->raw.is_valid()) {
				NHTTP_ENSURE(
					!handle->flags.io_mode,
					"please unwatch the socket before closing it!"
				);

				return handle->raw.close();
			}

			return false;
		}
	};


}