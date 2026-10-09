#include "game.h"

#include "board.h"
#include "book.h"
#include "chat.h"
#include "config.h"
#include "engine.h"
#include "json.h"
#include "lichess.h"
#include "platform.h"
#include "util.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <random>
#include <thread>

namespace
{
	void ensureEngine(GameContext &context);

	bool terminalStatus(std::string_view status)
	{
		return status != "created" && status != "started";
	}

	const json::Value *playerOf(const json::Value &event, const char *color)
	{
		if (const json::Value *side = event.find(color))
			return side;
		const json::Value *players = event.find("players");
		return players ? players->find(color) : nullptr;
	}

	std::string playerName(const json::Value &event, const char *color)
	{
		const json::Value *side = playerOf(event, color);
		if (side)
		{
			const std::string_view name = json::string(*side, "name");
			if (!name.empty())
				return std::string(name);
			if (const json::Value *user = side->find("user"))
				if (!json::string(*user, "name").empty())
					return std::string(json::string(*user, "name"));
		}
		return "opponent";
	}

	bool identifies(const json::Value &event, const char *color, std::string_view username)
	{
		const json::Value *side = playerOf(event, color);
		if (!side)
			return false;
		if (util::iequals(json::string(*side, "name"), username) || util::iequals(json::string(*side, "id"), username))
			return true;
		const json::Value *user = side->find("user");
		return user && (util::iequals(json::string(*user, "name"), username) || util::iequals(json::string(*user, "id"), username));
	}

	int playerRating(const json::Value &event, const char *color)
	{
		const json::Value *side = playerOf(event, color);
		return side ? static_cast<int>(json::integer(*side, "rating", 0)) : 0;
	}

	uint8_t promoOf(char letter)
	{
		switch (letter)
		{
		case 'n':
			return Board::KNIGHT;
		case 'b':
			return Board::BISHOP;
		case 'r':
			return Board::ROOK;
		case 'q':
			return Board::QUEEN;
		default:
			return 0;
		}
	}

	std::string nowStamp()
	{
		const std::time_t seconds = std::time(nullptr);
		std::tm parts{};
		plat::localTime(seconds, parts);
		char text[16];
		std::snprintf(text, sizeof(text), "%02d:%02d:%02d", parts.tm_hour, parts.tm_min, parts.tm_sec);
		return text;
	}

	std::string clockText(int64_t milliseconds)
	{
		const int64_t total = milliseconds > 0 ? milliseconds / 1000 : 0;
		char text[32];
		std::snprintf(text, sizeof(text), "%d:%02d", static_cast<int>(total / 60), static_cast<int>(total % 60));
		return text;
	}

	std::string scoreText(const SearchResult &result)
	{
		if (!result.scored)
			return "?";
		char text[16];
		if (result.mate != 0)
			std::snprintf(text, sizeof(text), "#%d", result.mate);
		else
			std::snprintf(text, sizeof(text), "%+.2f", result.cp / 100.0);
		return text;
	}

	std::string evalText(const SearchResult &result)
	{
		if (!result.scored)
			return "no evaluation";
		if (result.mate != 0)
			return result.mate > 0 ? "mate for us" : "mate against us";
		if (result.cp >= 400)
			return "we are winning";
		if (result.cp >= 120)
			return "we are better";
		if (result.cp >= 40)
			return "we are slightly better";
		if (result.cp > -40)
			return "balanced";
		if (result.cp > -120)
			return "we are slightly worse";
		if (result.cp > -400)
			return "opponent is better";
		return "opponent is winning";
	}

	std::string paintEval(const SearchResult &result)
	{
		const std::string text = evalText(result) + " (" + scoreText(result) + ")";
		if (!result.scored)
			return util::dim(text);
		if (result.mate != 0)
			return util::bold(result.mate > 0 ? util::green(text) : util::red(text));
		if (result.cp >= 40)
			return util::green(text);
		if (result.cp <= -40)
			return util::red(text);
		return util::yellow(text);
	}

	std::string moveLabel(size_t ply, const std::string &san)
	{
		return std::to_string((ply + 1) / 2) + (ply % 2 == 1 ? "." : "...") + san;
	}

	std::string compactCount(uint64_t value)
	{
		char text[32];
		if (value >= 1000000)
			std::snprintf(text, sizeof(text), "%.2fM", static_cast<double>(value) / 1000000.0);
		else if (value >= 10000)
			std::snprintf(text, sizeof(text), "%.1fK", static_cast<double>(value) / 1000.0);
		else if (value >= 1000)
			std::snprintf(text, sizeof(text), "%.2fK", static_cast<double>(value) / 1000.0);
		else
			std::snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(value));
		return text;
	}

	std::string secondsText(int64_t milliseconds)
	{
		char text[32];
		std::snprintf(text, sizeof(text), "%.1fs", static_cast<double>(milliseconds) / 1000.0);
		return text;
	}

	enum ChatCategory
	{
		CHAT_EDGE = 1,
		CHAT_WIN = 2,
		CHAT_CRUSH = 4,
		CHAT_DRAW = 8,
		CHAT_MATE = 16,
		CHAT_MATE_CLOSE = 32
	};

	const char *const chatEdgeLines[] = {
		"I think I have a small edge here.",
		"I feel like I have a slight edge at the moment.",
		"There is a small edge I am trying to nurse.",
		"I have a tiny edge and I want to keep it.",
		"This feels like a slight edge for me.",
		"I would say I have a small but real edge.",
		"An edge is slowly building up on my side.",
		"I have the slightly better position and a small edge.",
	};

	const char *const chatWinLines[] = {
		"I feel like I have a comfortable advantage here.",
		"My position is starting to feel like a clear advantage.",
		"Things are going my way, I think I have a real advantage.",
		"I am slowly building a nice advantage here.",
		"The advantage is clearly on my side now.",
		"I have a solid advantage and I intend to keep it.",
		"This position is giving me a clear and lasting advantage.",
		"I think I have built up a serious advantage by now.",
	};

	const char *const chatCrushLines[] = {
		"I think this game is practically decided.",
		"It is hard to see how I could let this one slip.",
		"This is completely winning for me now.",
		"I am converting a decisive position here.",
		"The game looks over unless I blunder badly.",
		"Everything is falling into place, this should be a win.",
		"I have a winning position and the rest is technique.",
		"This should be a win with careful play.",
	};

	const char *const chatDrawLines[] = {
		"I don't see a clear way forward for either side, this looks like a draw to me.",
		"This feels like it is heading for a draw.",
		"The position looks very balanced, probably a draw.",
		"Neither side seems able to make progress here, a draw looks likely.",
		"It is hard to imagine either side winning from here, a draw seems fair.",
		"We seem to have reached a balanced point, a draw feels like the natural result.",
		"With so little room to maneuver, this is drifting towards a draw.",
		"I would not be surprised if this ends in a draw.",
	};

	const char *const chatMateLines[] = {
		"I can see a forced mate from here.",
		"There is a mating line available, this should be over soon.",
		"I have spotted a forced mate.",
		"A mate is coming into view.",
		"The mating net is starting to close.",
		"I can already calculate a forced mate.",
		"It looks like there is a mate in the position.",
		"I think there is a forced mate hiding here.",
	};

	const char *const chatMateCloseLines[] = {
		"The mate is only a few moves away now.",
		"I am very close to delivering mate here.",
		"Just a couple of moves and it is mate.",
		"The mate is now unavoidable.",
		"It is mate in a few moves, I believe.",
		"The final mating sequence has begun.",
		"One more precise sequence and it is mate.",
		"Mate is just around the corner now.",
	};

	class Game
	{
	public:
		explicit Game(GameContext &context) : context(context), rng(std::random_device{}()) {}

		bool onEvent(const json::Value &event, bool &done);
		bool finished() const { return done.load(); }
		bool wantsReconnect() const { return reconnectWanted; }
		void clearReconnect() { reconnectWanted = false; }
		int eventsSeen() const { return events.load(); }
		std::string resultText() const;
		void showResult();
		void watchdogLoop(const std::atomic<bool> &stopFlag);
		bool rematchWanted() const;
		void offerRematch();

	private:
		void start(const json::Value &event);
		void update(const json::Value &state);
		void onChat(const json::Value &event);
		void onOpponentGone(const json::Value &event);
		void act();
		bool sendMove(std::string &move);
		std::string chooseMove();
		std::string bookMove();
		std::string engineMove();
		bool searchWith(Engine &engine, SearchResult &result, std::string &error);
		Board boardAt(size_t moveCount) const;
		Board buildBoard() const;
		std::string sanOf(const std::string &uci, size_t moveCount) const;
		void refreshStatus();
		void showProgress(const SearchResult &result);
		void postChat();
		void maybeOfferDraw();
		bool shouldAcceptDraw() const;
		std::string searchReport() const;
		ChatState chatState(const std::string &text) const;
		bool ourTurn() const;
		bool sideToMoveIsWhite() const;

		GameContext &context;
		std::mt19937_64 rng;
		std::string initialFen = "startpos";
		std::string variant = "standard";
		std::string opponentName = "opponent";
		std::string lastMoveText;
		std::string lastMoveStamp;
		SearchResult lastResult;
		int lastResultPly = 0;
		unsigned chatPosted = 0;
		int balanceStreak = 0;
		int edgeStreak = 0;
		int winStreak = 0;
		int crushStreak = 0;
		int deadDrawStreak = 0;
		int opponentRating = 0;
		int ourRating = 0;
		int initialClockMs = 0;
		int incrementMs = 0;
		int daysPerTurn = 0;
		int64_t lastReplyMs = 0;
		int64_t eventMs = 0;
		int64_t positionMs = 0;
		int64_t searchMs = 0;
		bool searched = false;
		int64_t progressStamp = 0;
		bool lastMoveOurs = false;
		bool white = true;
		bool clocked = false;
		bool started = false;
		bool ratedGame = false;
		bool opponentOfferedDraw = false;
		bool drawOfferedByUs = false;
		bool drawAccepted = false;
		bool reconnectWanted = false;
		std::vector<std::string> moves;
		int wtime = 0;
		int btime = 0;
		int winc = 0;
		int binc = 0;
		std::string status = "started";
		std::string winner;
		int acted = -1;
		int64_t stateStamp = 0;
		std::atomic<bool> done{ false };

		std::atomic<int> ply{ 0 };
		std::atomic<bool> opponentTurn{ false };
		std::atomic<bool> running{ false };
		std::atomic<int64_t> lastActivity{ 0 };
		std::atomic<bool> abortSent{ false };
		std::atomic<bool> claimArmed{ false };
		std::atomic<bool> claimed{ false };
		std::atomic<int64_t> claimAt{ 0 };
		std::atomic<int> events{ 0 };
	};

	bool Game::onEvent(const json::Value &event, bool &outDone)
	{
		events.fetch_add(1);
		eventMs = util::nowMs();
		const std::string_view type = json::string(event, "type");
		if (type == "gameFull" || (type.empty() && event.find("state") && event.find("initialFen")))
		{
			const json::Value *state = event.find("state");
			if (started)
			{
				util::info("game " + context.gameId + " state re-synced");
				update(state ? *state : event);
			}
			else
				start(event);
		}
		else if (type == "gameState")
			update(event);
		else if (type == "chatLine")
			onChat(event);
		else if (type == "opponentGone")
			onOpponentGone(event);
		else
			return true;

		if (terminalStatus(status))
		{
			done.store(true);
			outDone = true;
			return false;
		}
		if (status == "started" && ourTurn() && static_cast<int>(moves.size()) != acted)
		{
			act();
			if (reconnectWanted)
			{
				outDone = done.load();
				return false;
			}
		}

		outDone = done.load();
		return !done.load();
	}

	void Game::start(const json::Value &event)
	{
		started = true;
		if (const json::Value *variantValue = event.find("variant"))
			variant = std::string(json::string(*variantValue, "key", "standard"));
		initialFen = std::string(json::string(event, "initialFen", "startpos"));

		if (const json::Value *color = event.find("color"))
		{
			const std::string_view value = color->string();
			if (value == "white" || value == "black")
				white = value == "white";
		}
		else if (identifies(event, "white", context.username))
			white = true;
		else if (identifies(event, "black", context.username))
			white = false;
		else if (context.colorHint == "white" || context.colorHint == "black")
		{
			white = context.colorHint == "white";
			util::warn("game " + context.gameId + " colour taken from the gameStart event");
		}
		else
			util::warn("game " + context.gameId + " cannot determine our colour");

		if (const json::Value *clock = event.find("clock"))
		{
			initialClockMs = static_cast<int>(json::integer(*clock, "initial", 0));
			incrementMs = static_cast<int>(json::integer(*clock, "increment", 0));
			clocked = initialClockMs > 0;
		}
		ratedGame = json::flag(event, "rated", false);
		daysPerTurn = static_cast<int>(json::integer(event, "daysPerTurn", 0));
		opponentRating = playerRating(event, white ? "black" : "white");
		ourRating = playerRating(event, white ? "white" : "black");

		acted = -1;
		const json::Value *state = event.find("state");
		update(state ? *state : event);
		opponentName = playerName(event, white ? "black" : "white");

		std::string error;
		context.engine.setOption("UCI_Chess960", variant == "chess960" ? "true" : "false", error);
		util::info("game " + context.gameId + " start: " + variant + (white ? " white" : " black"));
		refreshStatus();
	}

	void Game::update(const json::Value &state)
	{
		const size_t previous = moves.size();
		moves.clear();
		const std::string list = std::string(json::string(state, "moves"));
		size_t start = 0;
		while (start < list.size())
		{
			while (start < list.size() && list[start] == ' ')
				++start;
			const size_t end = list.find(' ', start);
			if (start == end)
				break;
			moves.push_back(list.substr(start, end == std::string::npos ? std::string::npos : end - start));
			if (end == std::string::npos)
				break;
			start = end + 1;
		}

		clocked = clocked || state.find("wtime") != nullptr;
		wtime = static_cast<int>(json::integer(state, "wtime", wtime));
		btime = static_cast<int>(json::integer(state, "btime", btime));
		winc = static_cast<int>(json::integer(state, "winc", winc));
		binc = static_cast<int>(json::integer(state, "binc", binc));
		status = std::string(json::string(state, "status", status));
		winner = std::string(json::string(state, "winner", winner));
		stateStamp = util::nowMs();

		const bool whiteOffers = json::flag(state, "wdraw", false);
		const bool blackOffers = json::flag(state, "bdraw", false);
		opponentOfferedDraw = white ? blackOffers : whiteOffers;

		if (!moves.empty() && moves.size() != previous)
		{
			lastMoveText = moveLabel(moves.size(), sanOf(moves.back(), moves.size() - 1));
			lastMoveOurs = (moves.size() % 2 == 1) == white;
			lastMoveStamp = nowStamp();
		}
		lastActivity.store(util::nowMs());
		ply.store(static_cast<int>(moves.size()));
		opponentTurn.store(!ourTurn());
		running.store(status == "started");
		refreshStatus();
	}

	void Game::onChat(const json::Value &event)
	{
		const std::string_view user = json::string(event, "username");
		if (user.empty() || util::iequals(user, context.username))
			return;

		const std::string text = util::lower(util::trim(json::string(event, "text")));
		if (text.empty())
			return;

		const std::string_view room = json::string(event, "room", "player");
		const std::string target = room.empty() ? "player" : std::string(room);

		for (const ChatCommand &command : context.config.chatCommands)
		{
			if (!chat::triggerMatches(command, text))
				continue;
			if (util::nowMs() - lastReplyMs < 2000)
				return;

			const ChatState state = chatState(text);
			std::string conditionError;
			if (!chat::test(command.condition, state, conditionError))
			{
				if (!conditionError.empty())
					util::warn("game " + context.gameId + " chat_command '" + command.trigger + "': " + conditionError);
				return;
			}

			const std::vector<std::string> variants = chat::alternatives(command.response);
			if (variants.empty())
				return;

			lastReplyMs = util::nowMs();
			const std::string reply = chat::render(variants[rng() % variants.size()], state);
			std::string error;
			if (context.client.sendChat(context.gameId, target, reply, error))
				util::info("game " + context.gameId + " answered chat trigger '" + command.trigger + "' in " + target);
			else
				util::warn("game " + context.gameId + " chat reply failed: " + error);
			return;
		}

		if (text != "position?" && text != "position")
			return;

		if (util::nowMs() - lastReplyMs < 2000)
			return;
		lastReplyMs = util::nowMs();

		std::string error;
		if (context.client.sendChat(context.gameId, target, searchReport(), error))
			util::info("game " + context.gameId + " position report sent to " + target);
		else
			util::warn("game " + context.gameId + " position report failed: " + error);
	}

	void Game::onOpponentGone(const json::Value &event)
	{
		if (done.load())
			return;
		if (!json::flag(event, "gone", false))
		{
			if (claimArmed.exchange(false))
				util::info("game " + context.gameId + " opponent is back");
			return;
		}

		const int seconds = std::max(0, static_cast<int>(json::integer(event, "claimWinInSeconds", 0)));
		claimAt.store(util::nowMs() + static_cast<int64_t>(seconds) * 1000 + 2000);
		claimArmed.store(true);
		util::info("game " + context.gameId + " opponent left the game, claiming the win in "
		           + std::to_string(seconds) + "s");
	}

	std::string Game::searchReport() const
	{
		if (!lastResult.scored && lastResult.depth == 0)
			return "Search report · the engine has not searched yet.";

		std::string text = "Search report · depth " + std::to_string(lastResult.depth);
		if (lastResult.seldepth > lastResult.depth)
			text += "/" + std::to_string(lastResult.seldepth);
		text += " · eval " + scoreText(lastResult);
		if (!lastResult.line.empty())
			text += " · best " + sanOf(lastResult.line, static_cast<size_t>(std::max(0, lastResultPly)));

		text += "\nnodes " + compactCount(lastResult.nodes) + " · nps " + compactCount(lastResult.nps)
		      + " · hash " + std::to_string(lastResult.hashfull / 10) + "% · " + secondsText(lastResult.timeMs);
		return text;
	}

	ChatState Game::chatState(const std::string &text) const
	{
		ChatState state;
		state.text = text;
		state.ourTurn = ourTurn();
		state.rated = ratedGame;
		state.scored = lastResult.scored;
		state.cp = lastResult.cp;
		state.mate = lastResult.mate;
		state.depth = lastResult.depth;
		state.ply = static_cast<int>(moves.size());
		const int myTime = white ? wtime : btime;
		const int64_t remaining = std::max<int64_t>(0, myTime - (util::nowMs() - stateStamp));
		state.ourClockSeconds = static_cast<int>(remaining / 1000);

		const std::string best = lastResult.bestMove.empty() ? lastResult.line : lastResult.bestMove;
		state.set("eval", scoreText(lastResult));
		state.set("cp", std::to_string(lastResult.cp));
		state.set("mate", std::to_string(lastResult.mate));
		state.set("depth", std::to_string(lastResult.depth));
		state.set("seldepth", std::to_string(lastResult.seldepth));
		state.set("nodes", compactCount(lastResult.nodes));
		state.set("nps", compactCount(lastResult.nps));
		state.set("hash", std::to_string(lastResult.hashfull / 10) + "%");
		state.set("time", secondsText(lastResult.timeMs));
		state.set("clock", std::to_string(state.ourClockSeconds));
		state.set("best", best.empty() ? std::string() : sanOf(best, static_cast<size_t>(std::max(0, lastResultPly))));
		state.set("turn", sideToMoveIsWhite() ? "white" : "black");
		state.set("ply", std::to_string(moves.size()));
		state.set("last_move", lastMoveText);
		state.set("us", context.username);
		state.set("opponent", opponentName);
		state.set("our_rating", std::to_string(ourRating));
		state.set("opponent_rating", std::to_string(opponentRating));
		state.set("rated", ratedGame ? "rated" : "casual");
		state.set("result", resultText());
		state.set("report", searchReport());
		return state;
	}

	Board Game::boardAt(size_t moveCount) const
	{
		Board board;
		if (initialFen == "startpos")
			board.setFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
		else
			board.setFen(initialFen);
		for (size_t index = 0; index < moveCount && index < moves.size(); ++index)
			if (!board.apply(moves[index]))
				break;
		return board;
	}

	Board Game::buildBoard() const
	{
		return boardAt(moves.size());
	}

	std::string Game::sanOf(const std::string &uci, size_t moveCount) const
	{
		if (uci.size() < 4)
			return uci;
		const Board board = boardAt(moveCount);
		const int from = (uci[1] - '1') * 8 + (uci[0] - 'a');
		const int to = (uci[3] - '1') * 8 + (uci[2] - 'a');
		const uint8_t promo = uci.size() >= 5 ? promoOf(uci[4]) : 0;
		const std::string san = board.sanText(from, to, promo);
		return san.empty() ? uci : san;
	}

	void Game::refreshStatus()
	{
		const int myTime = white ? wtime : btime;
		const int64_t remaining = std::max<int64_t>(0, myTime - (util::nowMs() - stateStamp));
		const std::string clock = util::cyan(clockText(remaining) + " left");
		const std::string offer = opponentOfferedDraw ? " · " + util::yellow("opponent offers a draw") : "";

		if (lastMoveText.empty())
		{
			util::statusSet(0, util::bold(variant + " • " + (white ? "white" : "black")) + " · opponent " + util::bold(opponentName) + " · " + clock + offer);
			return;
		}

		const std::string marker = lastMoveOurs ? util::green("●") : util::dim("○");
		const std::string who = lastMoveOurs ? util::bold(context.username) : util::bold(opponentName);
		util::statusSet(0, util::dim(lastMoveStamp) + "  " + marker + " " + who + " " + util::bold(lastMoveText) + " played · position " + paintEval(lastResult) + " · " + clock + offer);
	}

	void Game::showProgress(const SearchResult &result)
	{
		std::string line = util::dim("⏳ thinking");
		if (result.depth > 0)
			line += " · depth " + std::to_string(result.depth);
		if (result.scored)
			line += " · " + paintEval(result);
		if (!result.line.empty())
			line += " · best " + util::bold(sanOf(result.line, moves.size()));
		util::statusSet(1, line);
	}

	void Game::postChat()
	{
		const Config &config = context.config;
		if (!config.chatEval || !lastResult.scored)
			return;

		if (lastResult.mate == 0)
		{
			edgeStreak = lastResult.cp >= config.chatEdgeCp ? edgeStreak + 1 : 0;
			winStreak = lastResult.cp >= config.chatWinCp ? winStreak + 1 : 0;
			crushStreak = lastResult.cp >= config.chatCrushCp ? crushStreak + 1 : 0;
			balanceStreak = std::abs(lastResult.cp) <= config.chatDrawCp ? balanceStreak + 1 : 0;
		}
		else
		{
			edgeStreak = 0;
			winStreak = 0;
			crushStreak = 0;
			balanceStreak = 0;
		}

		const unsigned mateTiers = CHAT_MATE | CHAT_MATE_CLOSE;
		unsigned category = 0;
		if (lastResult.mate > 0)
		{
			if (lastResult.mate <= config.chatMateClose && (chatPosted & CHAT_MATE) != 0 && (chatPosted & CHAT_MATE_CLOSE) == 0)
				category = CHAT_MATE_CLOSE;
			else if ((chatPosted & CHAT_MATE) == 0)
				category = CHAT_MATE;
		}
		else if (crushStreak >= config.chatCrushMoves && (chatPosted & (mateTiers | CHAT_CRUSH)) == 0)
			category = CHAT_CRUSH;
		else if (winStreak >= config.chatWinMoves && (chatPosted & (mateTiers | CHAT_CRUSH | CHAT_WIN)) == 0)
			category = CHAT_WIN;
		else if (edgeStreak >= config.chatEdgeMoves && (chatPosted & (mateTiers | CHAT_CRUSH | CHAT_WIN | CHAT_EDGE)) == 0)
			category = CHAT_EDGE;
		else if (balanceStreak >= config.chatDrawMoves && static_cast<int>(moves.size()) >= config.chatDrawMinPly && (chatPosted & CHAT_DRAW) == 0)
			category = CHAT_DRAW;
		if (category == 0)
			return;
		chatPosted |= category;

		const char *const *lines = chatEdgeLines;
		size_t count = std::size(chatEdgeLines);
		if (category == CHAT_WIN)
		{
			lines = chatWinLines;
			count = std::size(chatWinLines);
		}
		else if (category == CHAT_CRUSH)
		{
			lines = chatCrushLines;
			count = std::size(chatCrushLines);
		}
		else if (category == CHAT_DRAW)
		{
			lines = chatDrawLines;
			count = std::size(chatDrawLines);
		}
		else if (category == CHAT_MATE)
		{
			lines = chatMateLines;
			count = std::size(chatMateLines);
		}
		else if (category == CHAT_MATE_CLOSE)
		{
			lines = chatMateCloseLines;
			count = std::size(chatMateCloseLines);
		}

		const std::string text = lines[rng() % count];
		std::string error;
		if (!context.client.sendChat(context.gameId, "player", text, error))
			util::warn("game " + context.gameId + " player chat failed: " + error);
		if (!context.client.sendChat(context.gameId, "spectator", text, error))
			util::warn("game " + context.gameId + " spectator chat failed: " + error);
	}

	bool Game::ourTurn() const
	{
		return white == sideToMoveIsWhite();
	}

	bool Game::sideToMoveIsWhite() const
	{
		bool whiteToMove = true;
		if (initialFen != "startpos")
		{
			const size_t first = initialFen.find(' ');
			const size_t second = first == std::string::npos ? std::string::npos : initialFen.find(' ', first + 1);
			if (second != std::string::npos && second > first + 1)
				whiteToMove = initialFen[first + 1] == 'w';
		}
		return whiteToMove == (moves.size() % 2 == 0);
	}

	std::string Game::bookMove()
	{
		const Board board = buildBoard();
		uint16_t raw = 0;
		if (!context.book.pick(board.polyglotKey(), rng, raw))
			return {};

		const int to = raw & 0x3F;
		const int from = (raw >> 6) & 0x3F;
		const uint8_t promoPart = static_cast<uint8_t>((raw >> 12) & 7);
		const uint8_t promo = promoPart == 0 ? 0 : static_cast<uint8_t>(promoPart + 1);
		return board.moveText(from, to, promo, variant == "chess960");
	}

	bool Game::searchWith(Engine &engine, SearchResult &result, std::string &error)
	{
		const int64_t positionStart = util::nowMs();
		if (!engine.position(initialFen, moves, error))
			return false;
		positionMs = util::nowMs() - positionStart;

		const int myTime = white ? wtime : btime;
		const int myInc = white ? winc : binc;
		const int oppTime = white ? btime : wtime;
		const int oppInc = white ? binc : winc;

		std::string parameters;
		int timeoutMs = 0;

		if (clocked && (wtime > 0 || btime > 0))
		{
			const int64_t elapsed = util::nowMs() - stateStamp;
			int64_t budget = myTime - context.config.moveOverheadMs - elapsed;
			if (budget < 1)
				budget = 1;
			int64_t opponent = oppTime < 1 ? 1 : oppTime;
			parameters = "wtime " + std::to_string(white ? budget : opponent)
			           + " btime " + std::to_string(white ? opponent : budget)
			           + " winc " + std::to_string(white ? myInc : oppInc)
			           + " binc " + std::to_string(white ? oppInc : myInc);
			timeoutMs = static_cast<int>(budget * 2 + 5000);
		}
		else
		{
			int64_t movetime = context.config.correspondenceMovetimeMs;
			if (myTime > 0)
				movetime = std::min<int64_t>(movetime, std::max(1, myTime - context.config.moveOverheadMs));
			parameters = "movetime " + std::to_string(movetime);
			timeoutMs = static_cast<int>(movetime + 5000);
		}

		progressStamp = 0;
		const auto progress = [&](const SearchResult &current) {
			if (util::nowMs() - progressStamp < 120)
				return;
			progressStamp = util::nowMs();
			showProgress(current);
		};
		const int64_t searchStart = util::nowMs();
		const bool ok = engine.search(parameters, timeoutMs, result, progress, error);
		searchMs = util::nowMs() - searchStart;
		return ok;
	}

	std::string Game::engineMove()
	{
		std::string error;

		if (!context.engine.running())
		{
			ensureEngine(context);
			if (!context.engine.running())
			{
				util::fail("no engine available");
				return {};
			}
			context.engine.setOption("UCI_Chess960", variant == "chess960" ? "true" : "false", error);
		}

		SearchResult result;
		if (!searchWith(context.engine, result, error))
		{
			util::statusSet(1, "");
			util::fail("engine search failed: " + error);
			return {};
		}
		searched = true;

		lastResult = result;
		lastResultPly = static_cast<int>(moves.size());
		util::statusSet(1, "");
		if (result.bestMove == "0000")
			return {};
		return result.bestMove;
	}

	std::string Game::chooseMove()
	{
		if (context.book.ready() && static_cast<int>(moves.size()) < context.config.bookDepth)
		{
			const std::string move = bookMove();
			if (!move.empty())
				return move;
		}
		return engineMove();
	}

	bool Game::sendMove(std::string &move)
	{
		int status = 0;
		std::string error;
		if (context.client.makeMove(context.gameId, move, status, error))
			return true;

		if (status == 0 || status == 429)
		{
			util::warn("game " + context.gameId + " move " + move
			           + (status == 429 ? " rate limited: " : " transport error: ") + error + ", retrying");
			if (status == 0)
				util::sleepMs(300);
			if (context.client.makeMove(context.gameId, move, status, error))
			{
				util::info("game " + context.gameId + " move " + move + " (retry)");
				return true;
			}
			if (status == 0 || status == 429)
			{
				util::warn("game " + context.gameId + " move " + move + " retry failed: " + error);
				return false;
			}
		}

		util::warn("game " + context.gameId + " move " + move + " rejected: " + error);
		if (status != 400)
			return false;

		const std::string replacement = engineMove();
		if (!replacement.empty() && replacement != move)
		{
			if (context.client.makeMove(context.gameId, replacement, status, error))
			{
				move = replacement;
				util::info("game " + context.gameId + " move " + replacement + " (engine fallback)");
				return true;
			}
			util::warn("game " + context.gameId + " fallback move rejected: " + error);
		}
		return false;
	}

	void Game::act()
	{
		searched = false;
		positionMs = 0;
		searchMs = 0;
		const int64_t actStart = util::nowMs();
		std::string move = chooseMove();
		const int64_t thinkMs = util::nowMs() - actStart;
		if (move.empty())
		{
			util::fail("game " + context.gameId + " has no move, resigning");
			std::string error;
			context.client.resign(context.gameId, error);
			done.store(true);
			return;
		}

		if (searched && shouldAcceptDraw())
		{
			std::string error;
			if (context.client.offerDraw(context.gameId, error))
			{
				drawAccepted = true;
				util::info("game " + context.gameId + " draw offer accepted (dead draw)");
				return;
			}
			util::warn("game " + context.gameId + " draw acceptance failed: " + error);
		}

		if (sendMove(move))
		{
			std::string timing = "  " + secondsText(util::nowMs() - eventMs)
			                     + " · think " + secondsText(thinkMs);
			if (positionMs > 0)
				timing += " (position " + secondsText(positionMs) + " · search " + secondsText(searchMs) + ")";
			timing += " · send " + secondsText(util::nowMs() - actStart - thinkMs);
			util::info("game " + context.gameId + " move " + move + util::dim(timing));
			lastMoveText = moveLabel(moves.size() + 1, sanOf(move, moves.size()));
			lastMoveOurs = true;
			lastMoveStamp = nowStamp();
			refreshStatus();
			acted = static_cast<int>(moves.size());
			if (searched)
			{
				postChat();
				maybeOfferDraw();
			}
		}
		else
		{
			reconnectWanted = true;
		}
	}

	bool Game::shouldAcceptDraw() const
	{
		const Config &config = context.config;
		if (!config.drawAccept || !opponentOfferedDraw || drawAccepted)
			return false;
		if (!lastResult.scored || lastResult.mate != 0)
			return false;
		return std::abs(lastResult.cp) <= config.drawAcceptCp;
	}

	void Game::maybeOfferDraw()
	{
		const Config &config = context.config;
		if (!config.drawOffer || drawOfferedByUs || drawAccepted)
			return;

		if (!lastResult.scored || lastResult.mate != 0 || std::abs(lastResult.cp) > config.drawOfferCp)
		{
			deadDrawStreak = 0;
			return;
		}

		++deadDrawStreak;
		if (deadDrawStreak < config.drawOfferMoves || static_cast<int>(moves.size()) < config.drawOfferMinPly)
			return;

		std::string error;
		if (context.client.offerDraw(context.gameId, error))
		{
			drawOfferedByUs = true;
			util::info("game " + context.gameId + " position is a dead draw, offering a draw");
		}
		else
			util::warn("game " + context.gameId + " draw offer failed: " + error);
	}

	void Game::watchdogLoop(const std::atomic<bool> &stopFlag)
	{
		const int64_t idleLimitMs = static_cast<int64_t>(context.config.opponentIdleAbortSeconds) * 1000;
		const bool claimEnabled = context.config.claimWin;

		while (!stopFlag.load() && !done.load())
		{
			util::sleepMs(400);
			if (stopFlag.load() || done.load())
				break;

			std::string error;

			if (claimEnabled && claimArmed.load() && !claimed.load() && util::nowMs() >= claimAt.load())
			{
				claimed.store(true);
				if (context.client.claimVictory(context.gameId, error))
					util::warn("game " + context.gameId + " opponent did not come back, claiming the win");
				else
					util::warn("game " + context.gameId + " claim victory failed: " + error);
			}

			if (idleLimitMs <= 0 || abortSent.load() || !running.load() || !opponentTurn.load() || ply.load() >= 2)
				continue;
			const int64_t idle = util::nowMs() - lastActivity.load();
			if (idle < idleLimitMs)
				continue;

			abortSent.store(true);
			if (context.client.abort(context.gameId, error))
				util::warn("game " + context.gameId + " opponent idle for " + std::to_string(idle / 1000) + "s, aborting");
			else
				util::warn("game " + context.gameId + " abort failed: " + error);
		}
	}

	bool Game::rematchWanted() const
	{
		const Config &config = context.config;
		if (!config.rematch || daysPerTurn > 0 || initialClockMs <= 0)
			return false;
		if (status == "aborted" || status == "noStart")
			return false;
		if (opponentName == "opponent" || opponentName.empty())
			return false;
		if (opponentRating <= 0 || ourRating <= 0)
			return false;

		const std::string outcome = winner.empty() ? "draw" : (winner == (white ? "white" : "black") ? "win" : "lose");
		if (std::find(config.rematchOn.begin(), config.rematchOn.end(), outcome) == config.rematchOn.end())
			return false;

		const int ratingDiff = ourRating - opponentRating;
		if (ratingDiff < config.rematchMinRatingDiff)
			return false;
		if (config.rematchMaxRatingDiff > 0 && ratingDiff > config.rematchMaxRatingDiff)
			return false;
		return true;
	}

	void Game::offerRematch()
	{
		std::string color = "random";
		if (context.config.rematchColor == "same")
			color = white ? "white" : "black";
		else if (context.config.rematchColor == "opposite")
			color = white ? "black" : "white";

		const std::string body = util::formEncode({
			{ "rated", ratedGame ? "true" : "false" },
			{ "clock.limit", std::to_string(initialClockMs / 1000) },
			{ "clock.increment", std::to_string(incrementMs / 1000) },
			{ "color", color },
			{ "variant", variant },
		});

		std::string error;
		if (context.client.createChallenge(opponentName, body, error))
			util::info("game " + context.gameId + " rematch offered to " + opponentName
			           + " (" + std::to_string(initialClockMs / 1000) + "+" + std::to_string(incrementMs / 1000) + ")");
		else
			util::warn("game " + context.gameId + " rematch failed: " + error);
	}

	std::string Game::resultText() const
	{
		std::string text = status;
		if (!winner.empty())
			text += " winner " + winner;
		return text;
	}

	void Game::showResult()
	{
		util::statusSet(1, "");

		std::string verdict;
		if (winner.empty())
			verdict = status == "aborted" ? util::dim("aborted") : util::yellow("draw");
		else
			verdict = winner == (white ? "white" : "black") ? util::bold(util::green("we won")) : util::bold(util::red("we lost"));

		const std::string header = util::bold(variant + " • " + (white ? "white" : "black")) + " · opponent " + util::bold(opponentName);
		if (lastMoveText.empty())
			util::statusSet(0, header + " · game over: " + verdict);
		else
			util::statusSet(0, header + " · " + util::bold(lastMoveText) + " · game over: " + verdict);
	}

	void ensureEngine(GameContext &context)
	{
		if (context.engine.running())
			return;
		std::string error;
		if (!startEngine(context.engine, makeEngineSettings(context.config), error))
			util::fail("cannot start " + context.config.enginePath + ": " + error);
		else
			util::info("engine ready: " + context.config.enginePath);
	}
}

void playGame(GameContext &context)
{
	Game game(context);
	std::string error;

	ensureEngine(context);
	context.engine.newGame(error);

	std::atomic<bool> watchdogStop{ false };
	std::thread watchdog([&] { game.watchdogLoop(watchdogStop); });

	int attempt = 0;
	while (!context.stop && !game.finished())
	{
		const int seen = game.eventsSeen();
		game.clearReconnect();

		int status = 0;
		context.client.gameStream(context.gameId, [&](const json::Value &event) {
			bool done = false;
			game.onEvent(event, done);
			return !done;
		}, context.stop, status, error);

		if (game.finished() || context.stop)
			break;

		if (status == 401 || status == 403 || status == 404)
		{
			util::warn("game " + context.gameId + " stream rejected with " + std::to_string(status) + ", giving up");
			break;
		}

		if (game.eventsSeen() > seen)
			attempt = 0;
		++attempt;

		const bool lost = game.wantsReconnect();
		game.clearReconnect();
		const int64_t waitMs = std::min<int64_t>(30000, 1000LL << std::min(attempt, 5));
		util::warn("game " + context.gameId + " stream ended ("
		           + (error.empty() ? "http " + std::to_string(status) : error) + ")"
		           + (lost ? ", the move was not delivered" : "")
		           + ", reconnecting in " + std::to_string(waitMs / 1000) + "s");
		for (int64_t waited = 0; waited < waitMs && !context.stop; waited += 200)
			util::sleepMs(200);
	}

	watchdogStop.store(true);
	watchdog.join();

	util::info("game " + context.gameId + " over: " + game.resultText());
	game.showResult();
	if (!context.stop && game.rematchWanted())
		game.offerRematch();
	ensureEngine(context);
	context.engine.newGame(error);
}
