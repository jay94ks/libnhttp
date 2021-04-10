#include "listener_base.hpp"
#include "session_base.hpp"

namespace nhttp {
namespace base {

	void listener_base::terminate() {
		if (sources.size() || alives) {
			for (auto& each : sources) {
				watcher.unwatch(each);
				each.close();
			}

			sources.clear();

			terminating.store(true);
			while (alives)
				watcher.wait(10);

			while (pooled_tags.size()) {
				delete pooled_tags.front();
				pooled_tags.pop();
			}
		}
	}

	/* register new listening IPv4 address and ports. */
	bool listener_base::with(const ipv4& ep) {
		socket_t sock = socket_t::create<ipv4_addr, tcp>();
		hal::socket_raw_t raw = sock.get_raw();

		if (!sock) {
			return false;
		}

		if (!sock.bind(ep) || !sock.listen(10)) {
			sock.close();
			return false;
		}

		sock.set_tag(dynamic_cast<listener_base*>(this), nullptr);
		raw.set_non_blocking(true);
		raw.set_reuse_address(true);

		sources.push_back(sock);
		watcher.watch(sock, [](socket_t sock) {
			if (sock.can_read()) {
				((listener_base*)sock.get_tag())
					->on_newbie(sock);
			}
		});

		return true;
	}

	/* register new listening IPv6 address and ports. */
	bool listener_base::with(const ipv6& ep) {
		socket_t sock = socket_t::create<ipv6_addr, tcp>();
		hal::socket_raw_t raw = sock.get_raw();

		if (!sock) {
			return false;
		}

		if (!sock.bind(ep) || !sock.listen(10)) {
			sock.close();
			return false;
		}

		sock.set_tag(dynamic_cast<listener_base*>(this), nullptr);
		raw.set_non_blocking(true);
		raw.set_reuse_address(true);

		sources.push_back(sock);
		watcher.watch(sock, [](socket_t sock) {
			if (sock.can_read()) {
				((listener_base*)sock.get_tag())
					->on_newbie(sock);
			}
		});

		return true;
	}

	void listener_base::on_newbie(socket_t sock) {
		if (terminating) return;
		session_base* session = on_enter();

		/**
		 * if implementation can handle new connection, 
		 * accept newbie and encapsulate socket to link.
		 */
		if (!session) return;
		socket_t newbie = sock.accept();
		socket_tag* new_tag;

		/* pop a tag or create new one. */
		if (!pooled_tags.empty()) {
			new_tag = pooled_tags.front();
			pooled_tags.pop();
		}

		else new_tag = new socket_tag();

		/* set socket tag. */
		new_tag->session = session;
		new_tag->listener = this;
		newbie.set_tag(new_tag, nullptr);
			
		/* increase alive link counter. */
		++alives;

		/* handle init and watch on worker thread. */
		new_tag->initiator = workers->future_of([this, session, newbie]() {
			/* then initialize the link. */
			session->on_initiate(newbie, workers);
		});
		
		/* then, add newbie to watcher. */
		watcher.watch(newbie, [](socket_t s) {
			if (socket_tag* tag = (socket_tag*)s.get_tag()) {
				listener_base* listener = tag->listener;

				if (tag->initiator) {
					if (tag->initiator.is_completed())
						tag->initiator = nullptr;

					else return;
				}

				if (!s.is_alive() || !tag->session->on_event())
					listener->on_dead(tag, s);
			}
		});
	}

	void listener_base::on_dead(socket_tag* tag, socket_t& sock) {
		session_base* session = tag->session;

		/* remove tag from socket and return tag back first. */
		sock.set_tag(nullptr, nullptr);
		memset(tag, 0, sizeof(socket_tag));

		/* if terminating, delete tag immediately.*/
		if (!terminating && pooled_tags.size() <= 128)
			pooled_tags.push(tag);

		else delete tag;

		/* handle de-init on worker thread. */
		workers->future_of([this, tag, session]() {
			/* de-initialize the link. */
			session->on_finalize();

			/* then notify link leave. */
			on_leave(session);

			--alives;
		});

		watcher.unwatch(sock);
	}
}
}