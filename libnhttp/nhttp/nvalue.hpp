#pragma once
#include "types.hpp"
#include "utils/strings.hpp"

namespace nhttp {

	/**
	 */
	enum class nval {
		none = 0,
		integer,
		boolean,
		string,
		vector,
		map
	};

	/**
	 * class nvalue_base.
	 * interface for a value.
	 */
	class nvalue_base {
	public:
		virtual ~nvalue_base() { }

	public:
		virtual nval get_type() const = 0;
	};

	/**
	 * class nvalue< type_code >
	 * box object for storing a value.
	 */
	template<nval type>
	class nvalue;

	template<nval type>
	using nvalue_ptr = std::shared_ptr<nvalue<type>>;

	template<nval type>
	class nvalue : public nvalue_base {
	public:	virtual nval get_type() const override { return nval::none; }
	public:
		nvalue(...) { }
		inline bool as_bool() const { return false; }
		inline int64_t as_integer() const { return 0; }
		inline std::string as_string() const { return ""; }
		
		template<nval other_type>
		nvalue(const nvalue<other_type>& o) { }

		template<nval other_type>
		nvalue(const nvalue_ptr<other_type>& o) { }
	};

	template<>
	class nvalue<nval::boolean> : public nvalue_base {
	public:	virtual nval get_type() const override { return nval::boolean; }
	public: bool value; 
	public:
		nvalue(bool value = false) : value(value) { }
		inline bool as_bool() const { return value; }
		inline int64_t as_integer() const { return value ? 1 : 0; }
		inline std::string as_string() const { return value ? "true" : "false"; }

		template<nval other_type>
		nvalue(const nvalue<other_type>& o) : value(o.as_bool()) { }

		template<nval other_type>
		nvalue(const nvalue_ptr<other_type>& o) : value(o && o->as_bool()) { }
	};

	template<>
	class nvalue<nval::integer> : public nvalue_base {
	public:	virtual nval get_type() const override { return nval::integer; }
	public: int64_t value;
	public:
		nvalue(int64_t value = 0) : value(value) { }
		inline bool as_bool() const { return value ? true : false; }
		inline int64_t as_integer() const { return value; }
		inline std::string as_string() const { return std::to_string(value); }

		template<nval other_type>
		nvalue(const nvalue<other_type>& o) : value(o.as_integer()) { }

		template<nval other_type>
		nvalue(const nvalue_ptr<other_type>& o) : value(o && o->as_integer()) { }
	};

	template<>
	class nvalue<nval::string> : public nvalue_base {
	public:	virtual nval get_type() const override { return nval::string; }
	public: std::string value;
	public:
		nvalue() { }
		nvalue(const std::string& value) : value(value) { }
		nvalue(std::string&& value) : value(std::move(value)) { }
		inline bool as_bool() const { return value == "true"; }
		inline int64_t as_integer() const { return to_int64(value); }
		inline std::string as_string() const { return value; }

		template<nval other_type>
		nvalue(const nvalue<other_type>& o) : value(o.as_string()) { }
		
		template<nval other_type>
		nvalue(const nvalue_ptr<other_type>& o) : value(o && o->as_string()) { }
	};

	template<>
	class nvalue<nval::vector> : public nvalue_base {
	public: using vector = std::vector<std::shared_ptr<nvalue_base>>;
	public:	virtual nval get_type() const override { return nval::vector; }
	public: vector value;
	public:
		nvalue() { }
		nvalue(const vector& value) : value(value) { }
		nvalue(vector&& value) : value(std::move(value)) { }
		inline bool as_bool() const { return value.size() > 0; }
		inline int64_t as_integer() const { return 0; }
		inline std::string as_string() const { return "[nval::vector]"; }
		
		nvalue(const nvalue_ptr<nval::vector>& o) : value(o ? o->value : vector()) { }

	public:
		template<nval type>
		inline void push_back(const nvalue<type>& value) {
			this->value.push_back(std::make_shared<nvalue<type>>(value));
		}

		template<nval type>
		inline void push_back(nvalue<type>&& value) {
			this->value.push_back(std::make_shared<nvalue<type>>(std::move(value)));
		}
	};

	template<>
	class nvalue<nval::map> : public nvalue_base {
	public: using map = std::map<std::string, std::shared_ptr<nvalue_base>>;
	public:	virtual nval get_type() const override { return nval::map; }
	public: map value;
	public:
		nvalue() { }
		nvalue(const map& value) : value(value) { }
		nvalue(map&& value) : value(std::move(value)) { }
		inline bool as_bool() const { return value.size() > 0; }
		inline int64_t as_integer() const { return 0; }
		inline std::string as_string() const { return "[nval::map]"; }

		nvalue(const nvalue_ptr<nval::map>& o) : value(o ? o->value : map()) { }

	public:
		template<nval type>
		inline void emplace(std::string key, const nvalue<type>& value) {
			this->value.emplace(std::move(key), std::make_shared<nvalue<type>>(value));
		}

		template<nval type>
		inline void emplace(std::string key, nvalue<type>&& value) {
			this->value.emplace(std::move(key), std::make_shared<nvalue<type>>(std::move(value)));
		}
	};

}