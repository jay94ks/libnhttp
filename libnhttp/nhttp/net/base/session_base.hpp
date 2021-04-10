#pragma once
#include "../socket.hpp"
#include "../../asyncs/context.hpp"

namespace nhttp {
namespace base {

	class listener_base;

	/**
	 * class session_base.
	 * base class of a session.
	 */
	class NHTTP_API session_base {
		friend class listener_base;

	public:
		session_base() { }
		virtual ~session_base() { }

	protected:
		socket_t socket;
		std::shared_ptr<asyncs::context> asyncs;

	public:
		/* get socket instance. */
		inline const socket_t& get_socket() const { return socket; }

		/* get async worker. */
		inline const std::shared_ptr<asyncs::context>& get_asyncs() const { return asyncs; }

	protected:
		/* called when the session should be initialized with socket. */
		virtual void on_initiate(const socket_t& socket, const std::shared_ptr<asyncs::context>& asyncs) {
			this->socket = socket;
			this->asyncs = asyncs;
		}

		/* called when the session should be de-initialized.*/
		virtual void on_finalize() {
			this->socket = socket_t();
			this->asyncs = nullptr;
		}

		/**
		 * called when the session can read or write a socket.
		 * @returns:
		 *	= true : keep session alive.
		 *  = false: kill session.
		 */
		virtual bool on_event() = 0;
	};

}
}