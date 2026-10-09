#pragma once

#include <cstdint>
#include <string>
#include <string_view>

struct Board
{
	enum : uint8_t
	{
		EMPTY = 0,
		PAWN = 1,
		KNIGHT = 2,
		BISHOP = 3,
		ROOK = 4,
		QUEEN = 5,
		KING = 6,
		WHITE = 8
	};

	uint8_t squares[64] = {};
	bool whiteToMove = true;
	uint8_t castling = 0;
	int8_t rookOrigin[4] = { 7, 0, 63, 56 };
	int8_t ep = -1;

	bool setFen(std::string_view fen);
	bool apply(std::string_view uci);
	bool applyMove(int from, int to, uint8_t promo);
	uint64_t polyglotKey() const;
	std::string moveText(int from, int to, uint8_t promo, bool chess960) const;
	std::string sanText(int from, int to, uint8_t promo) const;

	static bool sameColour(uint8_t piece, bool white) { return (piece & WHITE) == (white ? WHITE : 0); }
};
