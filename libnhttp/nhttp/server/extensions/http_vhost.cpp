#include "http_vhost.hpp"
#include "../../hal/rwlock_t.hpp"

namespace nhttp {
namespace server {

	http_vhost::http_vhost(vhost_t, bool is_static) 
		: http_extendable_extension(is_static ? 0xf0000000 : 0xf0000001)
	{
	}

	http_vhost::http_vhost(std::string name) : http_vhost(by_vhost, true) {
		_predicate.callable = new std::string(std::move(name));
		_predicate.dtor = [](void* p) { delete (std::string*)p; };
		_predicate.call = [](void* p, const http_context_ptr& c) {
			const std::string& name = *(std::string*)p;
			const std::string& host = c->request->get_hostname();

			if (name.size() != host.size() ||
				strnicmp(name.c_str(), host.c_str(), name.size()))
				return false;

			return true;
		};
	}

	http_vhost::http_vhost(std::regex regex) : http_vhost(by_vhost, false) {
		_predicate.callable = new std::regex(std::move(regex));
		_predicate.dtor = [](void* p) { delete (std::regex*)p; };
		_predicate.call = [](void* p, const http_context_ptr& c) {
			const std::regex& regex = *(std::regex*)p;
			const std::string& host = c->request->get_hostname();

			return (bool)std::regex_match(host, regex);
		};
	}

	http_vhost::~http_vhost() {
		if (_predicate.dtor) {
			_predicate.dtor(_predicate.callable);
			_predicate.dtor = nullptr;
		}
	}

	bool http_vhost::on_collect(std::shared_ptr<http_context> context) {
		if (_predicate.call) {
			return _predicate.call(_predicate.callable, context);
		}

		return false;
	}

	bool http_vhost::on_enter(std::shared_ptr<http_context> context) {
		http_vhost_tag* tag = context->link->ensured_tag<http_vhost_tag>();

		/* set tag for notifying current vhost. */
		tag->vhosts.push(this);

		return true;
	}

	void http_vhost::on_leave(std::shared_ptr<http_context> context) {
		http_vhost_tag* tag = context->link->get_tag_ptr<http_vhost_tag>();

		if (tag) {
			tag->vhosts.pop();
		}
	}

}
}