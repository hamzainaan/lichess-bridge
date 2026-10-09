#include "config.h"

#include "util.h"

#include <charconv>

namespace
{
	bool parseBool(const std::string &value, bool &out)
	{
		const std::string lowered = util::lower(value);
		if (lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on")
		{
			out = true;
			return true;
		}
		if (lowered == "false" || lowered == "0" || lowered == "no" || lowered == "off")
		{
			out = false;
			return true;
		}
		return false;
	}

	bool parseInt(const std::string &value, int &out)
	{
		const auto result = std::from_chars(value.data(), value.data() + value.size(), out);
		return result.ec == std::errc() && result.ptr == value.data() + value.size();
	}

	bool apply(Config &config, const std::string &key, const std::string &value, std::string &error)
	{
		if (key == "token")
			config.token = value;
		else if (key == "base_url")
			config.baseUrl = value;
		else if (key == "engine")
			config.enginePath = value;
		else if (key == "engine_net")
			config.engineNet = value;
		else if (key == "engine_hash")
		{
			if (!parseInt(value, config.engineHash))
				return error = "invalid engine_hash '" + value + "'", false;
		}
		else if (key == "engine_threads")
		{
			if (!parseInt(value, config.engineThreads))
				return error = "invalid engine_threads '" + value + "'", false;
		}
		else if (key == "engine_option")
		{
			const size_t space = value.find_first_of(" \t");
			if (space == std::string::npos)
				return error = "engine_option needs '<name> <value>': " + value, false;
			config.engineOptions.emplace_back(util::trim(std::string_view(value).substr(0, space)),
			                                  util::trim(std::string_view(value).substr(space + 1)));
		}
		else if (key == "move_overhead")
		{
			if (!parseInt(value, config.moveOverheadMs))
				return error = "invalid move_overhead '" + value + "'", false;
		}
		else if (key == "correspondence_movetime")
		{
			if (!parseInt(value, config.correspondenceMovetimeMs))
				return error = "invalid correspondence_movetime '" + value + "'", false;
		}
		else if (key == "book")
			config.bookPath = value;
		else if (key == "book_depth")
		{
			if (!parseInt(value, config.bookDepth))
				return error = "invalid book_depth '" + value + "'", false;
		}
		else if (key == "chat_eval")
		{
			if (!parseBool(value, config.chatEval))
				return error = "invalid chat_eval '" + value + "'", false;
		}
		else if (key == "chat_edge_cp")
		{
			if (!parseInt(value, config.chatEdgeCp))
				return error = "invalid chat_edge_cp '" + value + "'", false;
		}
		else if (key == "chat_edge_moves")
		{
			if (!parseInt(value, config.chatEdgeMoves))
				return error = "invalid chat_edge_moves '" + value + "'", false;
		}
		else if (key == "chat_win_cp")
		{
			if (!parseInt(value, config.chatWinCp))
				return error = "invalid chat_win_cp '" + value + "'", false;
		}
		else if (key == "chat_win_moves")
		{
			if (!parseInt(value, config.chatWinMoves))
				return error = "invalid chat_win_moves '" + value + "'", false;
		}
		else if (key == "chat_crush_cp")
		{
			if (!parseInt(value, config.chatCrushCp))
				return error = "invalid chat_crush_cp '" + value + "'", false;
		}
		else if (key == "chat_crush_moves")
		{
			if (!parseInt(value, config.chatCrushMoves))
				return error = "invalid chat_crush_moves '" + value + "'", false;
		}
		else if (key == "chat_draw_cp")
		{
			if (!parseInt(value, config.chatDrawCp))
				return error = "invalid chat_draw_cp '" + value + "'", false;
		}
		else if (key == "chat_draw_moves")
		{
			if (!parseInt(value, config.chatDrawMoves))
				return error = "invalid chat_draw_moves '" + value + "'", false;
		}
		else if (key == "chat_draw_min_ply")
		{
			if (!parseInt(value, config.chatDrawMinPly))
				return error = "invalid chat_draw_min_ply '" + value + "'", false;
		}
		else if (key == "chat_mate_close")
		{
			if (!parseInt(value, config.chatMateClose))
				return error = "invalid chat_mate_close '" + value + "'", false;
		}
		else if (key == "chat_command")
		{
			ChatCommand command;
			if (!chat::parseCommand(value, command, error))
				return false;
			config.chatCommands.push_back(std::move(command));
		}
		else if (key == "accept_variants")
			config.acceptVariants = util::split(value, ',');
		else if (key == "accept_rated")
		{
			if (!parseBool(value, config.acceptRated))
				return error = "invalid accept_rated '" + value + "'", false;
		}
		else if (key == "accept_casual")
		{
			if (!parseBool(value, config.acceptCasual))
				return error = "invalid accept_casual '" + value + "'", false;
		}
		else if (key == "accept_correspondence")
		{
			if (!parseBool(value, config.acceptCorrespondence))
				return error = "invalid accept_correspondence '" + value + "'", false;
		}
		else if (key == "accept_from")
			config.acceptFrom = util::split(value, ',');
		else if (key == "accept_min_seconds")
		{
			if (!parseInt(value, config.acceptMinSeconds))
				return error = "invalid accept_min_seconds '" + value + "'", false;
		}
		else if (key == "accept_max_seconds")
		{
			if (!parseInt(value, config.acceptMaxSeconds))
				return error = "invalid accept_max_seconds '" + value + "'", false;
		}
		else if (key == "accept_min_increment")
		{
			if (!parseInt(value, config.acceptMinIncrement))
				return error = "invalid accept_min_increment '" + value + "'", false;
		}
		else if (key == "challenge_users")
			config.challengeUsers = util::split(value, ',');
		else if (key == "challenge_interval")
		{
			if (!parseInt(value, config.challengeIntervalSeconds))
				return error = "invalid challenge_interval '" + value + "'", false;
		}
		else if (key == "challenge_initial_delay")
		{
			if (!parseInt(value, config.challengeInitialDelaySeconds))
				return error = "invalid challenge_initial_delay '" + value + "'", false;
		}
		else if (key == "challenge_limit")
		{
			if (!parseInt(value, config.challengeLimitSeconds))
				return error = "invalid challenge_limit '" + value + "'", false;
		}
		else if (key == "challenge_increment")
		{
			if (!parseInt(value, config.challengeIncrementSeconds))
				return error = "invalid challenge_increment '" + value + "'", false;
		}
		else if (key == "challenge_rated")
		{
			if (!parseBool(value, config.challengeRated))
				return error = "invalid challenge_rated '" + value + "'", false;
		}
		else if (key == "challenge_variant")
			config.challengeVariant = value;
		else if (key == "challenge_variants")
			config.challengeVariants = util::split(value, ',');
		else if (key == "challenge_bots")
		{
			if (!parseBool(value, config.challengeBots))
				return error = "invalid challenge_bots '" + value + "'", false;
		}
		else if (key == "challenge_color")
			config.challengeColor = value;
		else if (key == "challenge_count")
		{
			if (!parseInt(value, config.challengeCount))
				return error = "invalid challenge_count '" + value + "'", false;
		}
		else if (key == "opponent_idle_abort")
		{
			if (!parseInt(value, config.opponentIdleAbortSeconds))
				return error = "invalid opponent_idle_abort '" + value + "'", false;
		}
		else if (key == "claim_win")
		{
			if (!parseBool(value, config.claimWin))
				return error = "invalid claim_win '" + value + "'", false;
		}
		else if (key == "draw_offer")
		{
			if (!parseBool(value, config.drawOffer))
				return error = "invalid draw_offer '" + value + "'", false;
		}
		else if (key == "draw_offer_cp")
		{
			if (!parseInt(value, config.drawOfferCp))
				return error = "invalid draw_offer_cp '" + value + "'", false;
		}
		else if (key == "draw_offer_moves")
		{
			if (!parseInt(value, config.drawOfferMoves))
				return error = "invalid draw_offer_moves '" + value + "'", false;
		}
		else if (key == "draw_offer_min_ply")
		{
			if (!parseInt(value, config.drawOfferMinPly))
				return error = "invalid draw_offer_min_ply '" + value + "'", false;
		}
		else if (key == "draw_accept")
		{
			if (!parseBool(value, config.drawAccept))
				return error = "invalid draw_accept '" + value + "'", false;
		}
		else if (key == "draw_accept_cp")
		{
			if (!parseInt(value, config.drawAcceptCp))
				return error = "invalid draw_accept_cp '" + value + "'", false;
		}
		else if (key == "rematch")
		{
			if (!parseBool(value, config.rematch))
				return error = "invalid rematch '" + value + "'", false;
		}
		else if (key == "rematch_on")
			config.rematchOn = util::split(value, ',');
		else if (key == "rematch_min_rating_diff")
		{
			if (!parseInt(value, config.rematchMinRatingDiff))
				return error = "invalid rematch_min_rating_diff '" + value + "'", false;
		}
		else if (key == "rematch_max_rating_diff")
		{
			if (!parseInt(value, config.rematchMaxRatingDiff))
				return error = "invalid rematch_max_rating_diff '" + value + "'", false;
		}
		else if (key == "rematch_color")
			config.rematchColor = value;
		else if (key == "upgrade_account")
		{
			if (!parseBool(value, config.upgradeAccount))
				return error = "invalid upgrade_account '" + value + "'", false;
		}
		else if (key == "request_interval")
		{
			if (!parseInt(value, config.requestIntervalMs))
				return error = "invalid request_interval '" + value + "'", false;
		}
		else if (key == "rate_limit_backoff")
		{
			if (!parseInt(value, config.rateLimitBackoffMs))
				return error = "invalid rate_limit_backoff '" + value + "'", false;
		}
		else
			return error = "unknown config key '" + key + "'", false;
		return true;
	}
}

bool Config::load(const std::string &path, std::string &error)
{
	std::string text;
	if (!util::readFile(path, text))
		return error = "cannot read config file " + path, false;

	size_t start = 0;
	while (start <= text.size())
	{
		const size_t end = text.find('\n', start);
		const std::string line = util::trim(std::string_view(text).substr(start, end == std::string::npos ? std::string::npos : end - start));
		start = end == std::string::npos ? text.size() + 1 : end + 1;

		if (line.empty() || line[0] == '#')
			continue;
		const size_t separator = line.find_first_of("=:");
		if (separator == std::string::npos)
			return error = "bad config line: " + line, false;
		const std::string key = util::lower(util::trim(std::string_view(line).substr(0, separator)));
		const std::string value = util::trim(std::string_view(line).substr(separator + 1));
		if (!apply(*this, key, value, error))
			return false;
	}
	const auto variantAllowed = [](const std::string &variant) {
		return variant == "standard" || variant == "chess960";
	};
	if (challengeVariants.empty())
	{
		if (!variantAllowed(challengeVariant))
			return error = "challenge_variant must be standard or chess960", false;
	}
	else
	{
		for (const std::string &variant : challengeVariants)
			if (!variantAllowed(variant))
				return error = "challenge_variants may only contain standard and chess960", false;
	}
	if (challengeBots && challengeIncrementSeconds < 1)
		return error = "challenge_increment must be at least 1 second when challenge_bots is enabled", false;
	if (chatWinMoves < 1 || chatDrawMoves < 1 || chatEdgeMoves < 1 || chatCrushMoves < 1)
		return error = "chat_edge_moves, chat_win_moves, chat_crush_moves and chat_draw_moves must be at least 1", false;
	if (chatDrawMinPly < 0 || chatMateClose < 0)
		return error = "chat_draw_min_ply and chat_mate_close must not be negative", false;
	if (drawOfferCp < 0 || drawAcceptCp < 0 || drawOfferMoves < 1 || drawOfferMinPly < 0)
		return error = "draw_offer_cp, draw_accept_cp, draw_offer_min_ply must not be negative and draw_offer_moves must be at least 1", false;
	if (opponentIdleAbortSeconds < 0)
		return error = "opponent_idle_abort must not be negative", false;
	if (requestIntervalMs < 0 || rateLimitBackoffMs < 0)
		return error = "request_interval and rate_limit_backoff must not be negative", false;
	for (const std::string &outcome : rematchOn)
		if (outcome != "win" && outcome != "lose" && outcome != "draw")
			return error = "rematch_on may only contain win, lose and draw", false;
	if (rematchColor != "random" && rematchColor != "same" && rematchColor != "opposite")
		return error = "rematch_color must be random, same or opposite", false;
	if (rematchMaxRatingDiff > 0 && rematchMinRatingDiff > rematchMaxRatingDiff)
		return error = "rematch_min_rating_diff must not exceed rematch_max_rating_diff", false;
	for (const ChatCommand &command : chatCommands)
	{
		std::string conditionError;
		chat::test(command.condition, ChatState{}, conditionError);
		if (!conditionError.empty())
			return error = "chat_command '" + command.trigger + "': " + conditionError, false;
		std::vector<std::string> missing;
		chat::render(command.response, ChatState{}, &missing);
		if (!missing.empty())
			return error = "chat_command '" + command.trigger + "': unknown placeholder {" + missing.front() + "}", false;
	}
	return true;
}
