#pragma once
#include "../types.hpp"
#include "../io/stream.hpp"
#include "../hal/spinlock_t.hpp"
#include "../protocol/http_status.hpp"
#include "../protocol/http_date.hpp"
#include "../protocol/http_resource.hpp"
#include "../protocol/http_headerset.hpp"
#include "../protocol/http_query_string.hpp"

namespace nhttp {
namespace server {

	class http_link;
	class http_raw_link;

	/**
	 * class http_raw_request.
	 * wraps request headers and its body.
	 * this contains only the request itself.
	 */
	class NHTTP_API http_raw_request {
	public:
		time_t						timestamp;
		http_resource				target;
		http_headers				headers;
		http_query_string			queries;
		std::shared_ptr<stream>		content;
	};

	/**
	 * class http_raw_response.
	 * wraps response headers and its body.
	 * this contains only the response itself.
	 */
	class NHTTP_API http_raw_response {
	public:
		http_status					status;
		http_headers				headers;
		std::shared_ptr<stream>		content;
	};

	/**
	 * class http_raw_context.
	 * wraps request and response.
	 */
	class NHTTP_API http_raw_context {
		friend class http_raw_link;

	public:
		int32_t						port			= 0;
		std::string					hostname;
		std::string					local_addr;
		std::string					remote_addr;
		http_raw_request			request;
		http_raw_response			response;
		bool						is_closed;
		bool						is_quiet;
		std::shared_ptr<http_link>	link;

	private:
		hal::spinlock_safe_t		spinlock;
		http_raw_link*				raw_link;
		bool						keepalive;
		void(* _close)(http_raw_context&);

	protected:
		inline bool is_keepalive() const { return keepalive; }

		/**
		 * configure raw_link to context.
		 */
		inline void configure(http_raw_link* raw_link, void(* _close)(http_raw_context&)) {
			std::lock_guard<decltype(spinlock)> guard(spinlock);
			this->raw_link = raw_link;
			this->_close = _close;
			is_quiet = false;
		}

		/**
		 * unconfigure raw_link from context.
		 */
		inline void unconfigure() {
			std::lock_guard<decltype(spinlock)> guard(spinlock);
			this->raw_link = nullptr;
			this->_close = nullptr;
			is_quiet = true;
		}

	public:
		/* send response and close context. */
		inline bool close(bool keepalive = true) {
			std::lock_guard<decltype(spinlock)> guard(spinlock);

			if (!is_closed) {
				this->keepalive = keepalive;
				is_closed = true;

				if (_close) {
					_close(*this);
					return true;
				}

				return false;
			}

			return false;
		}
	};

}
}