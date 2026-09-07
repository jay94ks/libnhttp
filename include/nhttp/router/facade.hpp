#pragma once

#include "middleware.hpp"
#include "target.hpp"
#include "../protocol/http_method.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace nhttp::router {

	/**
	 * class facade.
	 * the fluent registration DSL shared by every route node and by router
	 * itself — `router->get(...)->post(...)->group([...]{...})`. implemented by
	 * `route` (one tree node) and by `router` (forwards to its root node, but
	 * returns itself so chained calls keep registering siblings under the
	 * router rather than descending into whatever child the previous call
	 * touched).
	 */
	class facade {
	public:
		virtual ~facade() = default;

	public:
		virtual std::shared_ptr<facade> any(target_ptr t) = 0;
		virtual std::shared_ptr<facade> any(const std::string& path, target_ptr t) = 0;
		virtual std::shared_ptr<facade> method(const protocol::http_method& m, target_ptr t) = 0;
		virtual std::shared_ptr<facade> method(const protocol::http_method& m, const std::string& path, target_ptr t) = 0;

		/* sets a predicate constraining an existing ":name" parameter segment. */
		virtual std::shared_ptr<facade> param(const std::string& name, std::function<bool(std::string_view)> predicate) = 0;

		virtual std::shared_ptr<facade> prepend(middleware_ptr m) = 0;
		virtual std::shared_ptr<facade> append(middleware_ptr m) = 0;

		/* runs `body` against a proxy that records every route touched inside
		 * it, so a subsequent prepend()/append() on the RETURNED facade applies
		 * to all of them at once. */
		virtual std::shared_ptr<facade> group(std::function<void(std::shared_ptr<facade>)> body) = 0;

		/* resolves (creating intermediate nodes as needed) the facade for a
		 * path relative to this one, without registering anything on it.
		 * mainly for group()'s internal bookkeeping. */
		virtual std::shared_ptr<facade> resolve(const std::string& path) = 0;

	public:
		std::shared_ptr<facade> head(target_ptr t) { return method(protocol::http_method::HEAD(), std::move(t)); }
		std::shared_ptr<facade> options(target_ptr t) { return method(protocol::http_method::OPTIONS(), std::move(t)); }
		std::shared_ptr<facade> get(target_ptr t) { return method(protocol::http_method::GET(), std::move(t)); }
		std::shared_ptr<facade> post(target_ptr t) { return method(protocol::http_method::POST(), std::move(t)); }
		std::shared_ptr<facade> put(target_ptr t) { return method(protocol::http_method::PUT(), std::move(t)); }
		std::shared_ptr<facade> patch(target_ptr t) { return method(protocol::http_method::PATCH(), std::move(t)); }
		std::shared_ptr<facade> del(target_ptr t) { return method(protocol::http_method::DELETE(), std::move(t)); }

		std::shared_ptr<facade> head(const std::string& path, target_ptr t) { return method(protocol::http_method::HEAD(), path, std::move(t)); }
		std::shared_ptr<facade> options(const std::string& path, target_ptr t) { return method(protocol::http_method::OPTIONS(), path, std::move(t)); }
		std::shared_ptr<facade> get(const std::string& path, target_ptr t) { return method(protocol::http_method::GET(), path, std::move(t)); }
		std::shared_ptr<facade> post(const std::string& path, target_ptr t) { return method(protocol::http_method::POST(), path, std::move(t)); }
		std::shared_ptr<facade> put(const std::string& path, target_ptr t) { return method(protocol::http_method::PUT(), path, std::move(t)); }
		std::shared_ptr<facade> patch(const std::string& path, target_ptr t) { return method(protocol::http_method::PATCH(), path, std::move(t)); }
		std::shared_ptr<facade> del(const std::string& path, target_ptr t) { return method(protocol::http_method::DELETE(), path, std::move(t)); }
	};

	using facade_ptr = std::shared_ptr<facade>;

}
