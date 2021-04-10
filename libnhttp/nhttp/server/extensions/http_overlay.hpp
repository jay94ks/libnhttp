#pragma once
#include "http_extension.hpp"
#include "../../utils/lambda_t.hpp"

namespace nhttp {
namespace server {

	class http_overlay;
	using http_overlay_ptr = std::shared_ptr<http_overlay>;

	/**
	 * class http_overlay.
	 * file-system overlay extension.
	 * maps a directory to http path.
	 */
	class NHTTP_API http_overlay : public http_extension {
	public:
		typedef lambda_t<bool(const std::shared_ptr<http_context>&, const std::string&)> predicate_t;

		std::string base_dir;
		std::string index_file;
		predicate_t predicate;

	public:
		http_overlay(std::string base_dir);
		http_overlay(std::string base_dir, std::string index_file);
		virtual ~http_overlay() { }

	protected:
		virtual bool on_collect(std::shared_ptr<http_context> context) override { return true; }

	private:
		virtual bool on_handle(std::shared_ptr<http_context> context) override;
	};

	/* create http overlay to base_dir. */
	inline http_overlay_ptr overlay_of(std::string base_dir, std::string index_file) {
		return std::make_shared<http_overlay>(std::move(base_dir), std::move(index_file));
	}
}
}