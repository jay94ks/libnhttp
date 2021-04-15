#include "tests.hpp"
#include <nhttp/utils/urlencode.hpp>
#include <nhttp/utils/this_ptr.hpp>
#include <nhttp/utils/strings.hpp>
#include <nhttp/utils/path.hpp>
#include <nhttp/utils/lambda_t.hpp>
#include <nhttp/utils/instrusive.hpp>

/**
 * Test functions for utilities.
 */


void test_urlencode() {
	test_case label("utils/urlencode.hpp");

	auto encoded = nhttp::urlencode(u8"url encode test: 123456789");
	if (encoded != "url+encode+test%3a+123456789") {
		std::cout << " : result: " << encoded << "\n";
		std::cout << " : expect: " << "url+encode+test%3a+123456789\n";
	}

	auto decoded = nhttp::urldecode(encoded);
	if (decoded != "url encode test: 123456789") {
		std::cout << " : result: " << decoded << "\n";
		std::cout << " : expect: " << "url encode test: 123456789\n";
	}
}

void test_this_ptr() {
	class test_this {
	public:
		nhttp::this_ptr<test_this> test() { return this; }
	};

	test_case label("utils/this_ptr.hpp");

	auto* p = new test_this();
	if (&(*(p->test()->test())) != p) {
		std::cout << " - error. \n";
		std::cout << " : " << &(*(p->test()->test())) << " should be " << p << "\n";
	}

	delete p;
}

void test_strings() {
	test_case label("utils/strings.hpp");

	if (nhttp::strnicmp("abcd", "abcd", 4) != 0) {
		std::cout << " : strnicmp(\"abcd\", \"abcd\", 4) should return zero!\n";
	}

	if (nhttp::strnicmp("abcd", "abcd") != 0) {
		std::cout << " : strnicmp(\"abcd\", \"abcd\") should return zero!\n";
	}

	if (nhttp::stricmp("abcD", "abcd") != 0) {
		std::cout << " : stricmp(\"abcD\", \"abcd\") should return zero!\n";
	}

	if (nhttp::strcmp_x("abcD", "abcd") == 0) {
		std::cout << " : strcmp_x(\"abcD\", \"abcd\") should never return zero!\n";
	}

	if (nhttp::to_int64("1234", 10, 4) != 1234) {
		std::cout << " : to_int64(\"1234\", 10, 4) should return 1234!\n";
	}

	if (nhttp::to_int64("1234", 16, 4) != 0x1234) {
		std::cout << " : to_int64(\"1234\", 16, 4) should return " << 0x1234 << "\n";
	}

	if (nhttp::to_int64("1234+", 16, 4) != 0x1234) {
		std::cout << " : to_int64(\"1234+\", 16, 4) should return " << 0x1234 << "\n";
	}

	std::string tmp;
	char buf[128] = { 0, };

	nhttp::to_hex(tmp, 0x1234);
	nhttp::to_hex(buf, sizeof(buf), 0x1234);

	if (tmp != "1234") {
		std::cout << " : to_hex(xx, 0x1234) should return \"1234\"\n";
	}

	if (nhttp::stricmp(buf, "1234")) {
		std::cout << " : to_hex(buf, sz, 0x1234) should return \"1234\"\n";
	}

	tmp.clear();
	memset(buf, 0, sizeof(buf));

	nhttp::to_hex32(tmp, 0x1234);

	if (tmp != "1234") {
		std::cout << " : to_hex32(xx, 0x1234) should return \"1234\"\n";
	}

	if (nhttp::wcs_to_mbs(L"abcd 한글") != u8"abcd 한글") {
		std::cout << " : wcs_to_mbs(L\"abcd 한글\") should return u8\"abcd 한글\"\n";
	}

	if (nhttp::mbs_to_wcs(u8"abcd 한글") != L"abcd 한글") {
		std::cout << " : mbs_to_wcs(u8\"abcd 한글\") should return L\"abcd 한글\"\n";
	}

	if (nhttp::hash_djb(nullptr, 0) != 5381) {
		std::cout << " : hash_djb(nullptr, 0) should return 5381\n";
	}

	if (nhttp::hash_djb("abcd", 4) != 2090069583) {
		std::cout << " : hash_djb(\"abcd\", 4) should return 2090069583\n";
	}

	if (nhttp::stricmp(nhttp::ltrim(" \t \tabcd efg !\t"), "abcd efg !\t") != 0) {
		std::cout << " : ltrim(\" \\t \\tabcd efg !\\t\", 4) should return \"abcd efg !\\t\"\n";
	}
}

void test_paths() {
	test_case label("utils/path.hpp");

	if (nhttp::basepath("/abcde/ff") != "/abcde") {
		std::cout << " : basepath(/abcde/ff) should return /abcde\n";
	}

	if (nhttp::basepath("/abcde/") != "/") {
		std::cout << " : basepath(/abcde/) should return /\n";
	}

	if (nhttp::basename("/abcde/ff") != "ff") {
		std::cout << " : basename(/abcde/ff) should return ff\n";
	}

	if (nhttp::qualify_path("/abcde/./a/../ff") != "abcde/ff") {
		std::cout << " : basename(/abcde/./a/../ff) should return abcde/ff\n";
	}
}

void test_lambda() {
	test_case label("utils/lambda_t.hpp");

	nhttp::lambda_t<int(int)> k = [](int c) {
		return c + 100;
	};

	if (k(10) != 110) {
		std::cout << " : ([](int c) { return c + 100; })(10) should return 110\n";
	}

	nhttp::lambda_t<void(int&)> m = [](int& c) {
		c += 100;
	};

	int tmp = 10;
	m(tmp);

	if (tmp != 110) {
		std::cout << " : ([](int& c) { return c + 100; })(ref 10), c should be 110\n";
	}
}

void test_instrusive() {
	test_case label("utils/instrusive.hpp");

	nhttp::utils::instrusive<int> should_be_zero;

	if ((*should_be_zero)) {
		std::cout << " : (*nhttp::utils::instrusive<int>()) should be 0\n";
	}

	should_be_zero = 100;

	if ((*should_be_zero) != 100) {
		std::cout << " : ((*nhttp::utils::instrusive<int>()) = 100) should be 100\n";
	}

	if (should_be_zero.unset().ready) {
		std::cout << " : should_be_zero.unset().ready should be false\n";
	}

	should_be_zero = 100;
	if (!should_be_zero.ready) {
		std::cout << " : should_be_zero.set(100).ready should be true\n";
	}
}