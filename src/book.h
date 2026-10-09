#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

class Book
{
public:
	bool load(const std::string &path, std::string &error);
	bool ready() const { return !entries.empty(); }
	size_t size() const { return entries.size(); }
	bool pick(uint64_t key, std::mt19937_64 &rng, uint16_t &rawMove) const;

private:
	struct Entry
	{
		uint64_t key = 0;
		uint16_t move = 0;
		uint16_t weight = 0;
	};

	void loadPolyglot(const std::string &path);
	void loadEpd(std::string_view data);

	std::vector<Entry> entries;
};
