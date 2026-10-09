#pragma once

#include "chat.h"

#include <string>
#include <utility>
#include <vector>

struct Config
{
	std::string token;
	std::string baseUrl = "https://lichess.org";
	std::string enginePath;
	std::string engineNet;
	int engineHash = 64;
	int engineThreads = 1;
	std::vector<std::pair<std::string, std::string>> engineOptions;

	int moveOverheadMs = 150;
	int correspondenceMovetimeMs = 5000;
	std::string bookPath;
	int bookDepth = 20;
	bool chatEval = true;
	int chatEdgeCp = 60;
	int chatEdgeMoves = 3;
	int chatWinCp = 120;
	int chatWinMoves = 2;
	int chatCrushCp = 500;
	int chatCrushMoves = 1;
	int chatDrawCp = 25;
	int chatDrawMoves = 6;
	int chatDrawMinPly = 20;
	int chatMateClose = 3;
	std::vector<ChatCommand> chatCommands;

	bool drawOffer = true;
	int drawOfferCp = 15;
	int drawOfferMoves = 4;
	int drawOfferMinPly = 20;
	bool drawAccept = true;
	int drawAcceptCp = 30;
	int opponentIdleAbortSeconds = 60;
	bool claimWin = true;
	bool rematch = true;
	std::vector<std::string> rematchOn{ "win", "lose", "draw" };
	int rematchMinRatingDiff = 1;
	int rematchMaxRatingDiff = 0;
	std::string rematchColor = "random";

	std::vector<std::string> acceptVariants{ "standard" };
	bool acceptRated = true;
	bool acceptCasual = true;
	bool acceptCorrespondence = false;
	std::vector<std::string> acceptFrom;
	int acceptMinSeconds = 0;
	int acceptMaxSeconds = 0;
	int acceptMinIncrement = 0;

	std::vector<std::string> challengeUsers;
	int challengeIntervalSeconds = 600;
	int challengeInitialDelaySeconds = 10;
	int challengeLimitSeconds = 180;
	int challengeIncrementSeconds = 2;
	bool challengeRated = true;
	std::string challengeVariant = "standard";
	std::vector<std::string> challengeVariants;
	bool challengeBots = false;
	std::string challengeColor = "random";
	int challengeCount = 0;

	bool upgradeAccount = true;

	int requestIntervalMs = 100;
	int rateLimitBackoffMs = 60000;

	bool load(const std::string &path, std::string &error);
};
