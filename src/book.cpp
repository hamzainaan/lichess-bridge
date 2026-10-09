#include "book.h"

#include "board.h"
#include "util.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace
{
	uint16_t readBigEndian16(const unsigned char *bytes)
	{
		return uint16_t((uint16_t(bytes[0]) << 8) | uint16_t(bytes[1]));
	}

	uint64_t readBigEndian64(const unsigned char *bytes)
	{
		uint64_t value = 0;
		for (int i = 0; i < 8; ++i)
			value = (value << 8) | bytes[i];
		return value;
	}

	bool parseMove(std::string_view token, uint16_t &raw)
	{
		if (token.size() < 4)
			return false;
		const int fromFile = token[0] - 'a';
		const int fromRank = token[1] - '1';
		const int toFile = token[2] - 'a';
		const int toRank = token[3] - '1';
		if (fromFile < 0 || fromFile > 7 || fromRank < 0 || fromRank > 7 || toFile < 0 || toFile > 7 || toRank < 0 || toRank > 7)
			return false;

		uint8_t promo = 0;
		if (token.size() >= 5)
		{
			switch (token[4])
			{
			case 'n':
				promo = Board::KNIGHT;
				break;
			case 'b':
				promo = Board::BISHOP;
				break;
			case 'r':
				promo = Board::ROOK;
				break;
			case 'q':
				promo = Board::QUEEN;
				break;
			default:
				return false;
			}
		}

		const int from = fromRank * 8 + fromFile;
		const int to = toRank * 8 + toFile;
		raw = uint16_t(to | (from << 6) | ((promo == 0 ? 0 : promo - 1) << 12));
		return true;
	}
}

bool Book::load(const std::string &path, std::string &error)
{
	entries.clear();

	std::FILE *file = std::fopen(path.c_str(), "rb");
	if (!file)
		return error = "cannot read book " + path, false;

	unsigned char head[4096];
	const size_t headSize = std::fread(head, 1, sizeof(head), file);
	std::fclose(file);
	if (headSize == 0)
		return error = "empty book " + path, false;

	bool text = true;
	for (size_t i = 0; i < headSize && text; ++i)
		text = head[i] == '\t' || head[i] == '\n' || head[i] == '\r' || (head[i] >= 32 && head[i] < 127);

	if (text)
	{
		std::string data;
		if (!util::readFile(path, data))
			return error = "cannot read book " + path, false;
		loadEpd(data);
	}
	else
		loadPolyglot(path);

	const auto ordered = [](const Entry &a, const Entry &b) { return a.key < b.key; };
	if (!std::is_sorted(entries.begin(), entries.end(), ordered))
		std::sort(entries.begin(), entries.end(), ordered);
	return true;
}

void Book::loadPolyglot(const std::string &path)
{
	std::FILE *file = std::fopen(path.c_str(), "rb");
	if (!file)
	{
		util::warn("cannot open polyglot book " + path);
		return;
	}

	std::error_code code;
	const uintmax_t size = std::filesystem::file_size(path, code);
	if (!code && size % 16 != 0)
		util::warn("polyglot book size is not a multiple of 16 bytes, tail ignored");
	if (!code)
		entries.reserve(size / 16);

	unsigned char buffer[16384];
	size_t tailSize = 0;
	while (const size_t read = std::fread(buffer + tailSize, 1, sizeof(buffer) - tailSize, file))
	{
		const size_t available = tailSize + read;
		const size_t records = available / 16;
		for (size_t i = 0; i < records; ++i)
		{
			const unsigned char *bytes = buffer + i * 16;
			const uint16_t weight = readBigEndian16(bytes + 10);
			if (weight != 0)
				entries.push_back(Entry{ readBigEndian64(bytes), readBigEndian16(bytes + 8), weight });
		}
		tailSize = available % 16;
		std::memmove(buffer, buffer + records * 16, tailSize);
	}

	std::fclose(file);
}

void Book::loadEpd(std::string_view data)
{
	size_t start = 0;
	while (start <= data.size())
	{
		const size_t end = data.find('\n', start);
		const std::string line = util::trim(data.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
		start = end == std::string_view::npos ? data.size() + 1 : end + 1;

		if (line.empty() || line[0] == '#')
			continue;

		size_t position = 0;
		int fields = 0;
		while (fields < 4)
		{
			while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position])))
				++position;
			const size_t begin = position;
			while (position < line.size() && !std::isspace(static_cast<unsigned char>(line[position])))
				++position;
			if (begin == position)
				break;
			++fields;
		}
		if (fields < 4)
			continue;

		Board board;
		if (!board.setFen(std::string_view(line).substr(0, position)))
			continue;
		const uint64_t key = board.polyglotKey();

		bool collecting = false;
		bool firstOnly = false;
		size_t cursor = position;
		while (cursor < line.size())
		{
			while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == ','))
				++cursor;
			const size_t tokenStart = cursor;
			while (cursor < line.size() && line[cursor] != ' ' && line[cursor] != ',')
				++cursor;
			if (tokenStart == cursor)
				break;

			std::string_view token(line.data() + tokenStart, cursor - tokenStart);
			const bool endOfOpcode = token.back() == ';';
			if (endOfOpcode)
				token.remove_suffix(1);

			if (token == "bm" || token == "pv")
			{
				collecting = true;
				firstOnly = token == "pv";
			}
			else if (collecting && !token.empty())
			{
				uint16_t raw = 0;
				if (parseMove(token, raw))
				{
					entries.push_back(Entry{ key, raw, 1 });
					if (firstOnly)
						collecting = false;
				}
				else
					collecting = false;
			}
			if (endOfOpcode)
				collecting = false;
		}
	}
}

bool Book::pick(uint64_t key, std::mt19937_64 &rng, uint16_t &rawMove) const
{
	const auto begin = std::lower_bound(entries.begin(), entries.end(), key, [](const Entry &entry, uint64_t value) { return entry.key < value; });

	uint64_t total = 0;
	for (auto entry = begin; entry != entries.end() && entry->key == key; ++entry)
		total += entry->weight;
	if (total == 0)
		return false;

	uint64_t choice = rng() % total;
	for (auto entry = begin; entry != entries.end() && entry->key == key; ++entry)
	{
		if (choice < entry->weight)
		{
			rawMove = entry->move;
			return true;
		}
		choice -= entry->weight;
	}
	return false;
}
