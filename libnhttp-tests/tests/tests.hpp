#pragma once
#define __NHTTP_STATIC__
#include <nhttp/types.hpp>

#include <ctime>
#include <iostream>

class test_case {
private:
	const char* name;
	clock_t begin;
	bool no_print = false;

public:
	test_case(const char* name) 
		: name(name), begin(clock())
	{
		std::cout << "test for " << name << "\n";
	}

	~test_case() {
		if (!no_print) {
			double v = (clock() - begin) * 1.0 / CLOCKS_PER_SEC;
			std::cout << " + spent: " << v << " s\n";
		}
	}

public:
	void print_now() {
		double v = (clock() - begin) * 1.0 / CLOCKS_PER_SEC;
		std::cout << " + spent: " << v << " s\n";

		no_print = true;
		begin = clock();
	}
};

void test_urlencode();
void test_this_ptr();
void test_strings();
void test_paths();
void test_lambda();
void test_instrusive();


void test_async();
void test_hal();
void test_net();
void test_protocol();