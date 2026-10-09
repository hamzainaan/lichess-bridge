#include "book.h"
#include "config.h"
#include "engine.h"
#include "game.h"
#include "json.h"
#include "lichess.h"
#include "platform.h"
#include "util.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <deque>
#include <random>
#include <thread>

namespace
{
	std::atomic<bool> stopFlag{ false };

	void onSignal(int)
	{
		stopFlag.store(true);
	}

	std::string joinList(const std::vector<std::string> &items, const char *separator)
	{
		std::string out;
		for (const std::string &item : items)
		{
			if (!out.empty())
				out += separator;
			out += item;
		}
		return out;
	}

	enum MatchState
	{
		IDLE = 0,
		STARTING = 1,
		PLAYING = 2
	};

	struct ChallengeInfo
	{
		std::string id;
		std::string challenger;
		bool rated = false;
		std::string variant = "standard";
		int limitSeconds = 0;
		int incrementSeconds = 0;
		int days = 0;
	};

	ChallengeInfo parseChallenge(const json::Value &challenge)
	{
		ChallengeInfo info;
		info.id = std::string(json::string(challenge, "id"));
		info.rated = json::flag(challenge, "rated", false);
		if (const json::Value *variant = challenge.find("variant"))
			info.variant = std::string(json::string(*variant, "key", "standard"));
		if (const json::Value *challenger = challenge.find("challenger"))
			info.challenger = std::string(json::string(*challenger, "name"));
		if (const json::Value *timeControl = challenge.find("timeControl"))
		{
			info.limitSeconds = static_cast<int>(json::integer(*timeControl, "limit", 0));
			info.incrementSeconds = static_cast<int>(json::integer(*timeControl, "increment", 0));
			info.days = static_cast<int>(json::integer(*timeControl, "daysPerTurn", 0));
		}
		return info;
	}

	bool acceptsChallenge(const ChallengeInfo &challenge, const Config &config, std::string &reason)
	{
		if (!config.acceptFrom.empty())
		{
			bool allowed = false;
			for (const std::string &name : config.acceptFrom)
				if (util::iequals(name, challenge.challenger))
					allowed = true;
			if (!allowed)
			{
				reason = "generic";
				return false;
			}
		}

		bool variantAllowed = false;
		for (const std::string &variant : config.acceptVariants)
			if (variant == challenge.variant)
				variantAllowed = true;
		if (!variantAllowed)
		{
			reason = "variant";
			return false;
		}

		if (challenge.rated && !config.acceptRated)
		{
			reason = "rated";
			return false;
		}
		if (!challenge.rated && !config.acceptCasual)
		{
			reason = "casual";
			return false;
		}
		if (challenge.days > 0 && !config.acceptCorrespondence)
		{
			reason = "timeControl";
			return false;
		}
		if (config.acceptMinSeconds > 0 && challenge.limitSeconds < config.acceptMinSeconds)
		{
			reason = "tooFast";
			return false;
		}
		if (config.acceptMaxSeconds > 0 && challenge.limitSeconds > config.acceptMaxSeconds)
		{
			reason = "tooSlow";
			return false;
		}
		if (config.acceptMinIncrement > 0 && challenge.days == 0 && challenge.incrementSeconds < config.acceptMinIncrement)
		{
			reason = "timeControl";
			return false;
		}
		return true;
	}

	std::string challengeForm(const Config &config, const std::string &variant)
	{
		return util::formEncode({
			{ "rated", config.challengeRated ? "true" : "false" },
			{ "clock.limit", std::to_string(config.challengeLimitSeconds) },
			{ "clock.increment", std::to_string(config.challengeIncrementSeconds) },
			{ "color", config.challengeColor },
			{ "variant", variant },
		});
	}

	std::string pickVariant(const Config &config, std::mt19937_64 &rng)
	{
		if (config.challengeVariants.empty())
			return config.challengeVariant;
		return config.challengeVariants[rng() % config.challengeVariants.size()];
	}
}

int main(int argc, char **argv)
{
	plat::init();
#ifdef SIGPIPE
	std::signal(SIGPIPE, SIG_IGN);
#endif
	std::signal(SIGINT, onSignal);
	std::signal(SIGTERM, onSignal);

	const std::string configPath = argc > 1 ? argv[1] : "bridge.conf";
	Config config;
	std::string error;
	if (!config.load(configPath, error))
	{
		util::fail(error);
		return 1;
	}
	if (const char *token = std::getenv("LICHESS_TOKEN"); token && *token)
		config.token = token;
	if (config.token.empty() || config.enginePath.empty())
	{
		util::fail("token and engine are required in " + configPath);
		return 1;
	}

	http::setRateLimit(config.requestIntervalMs, config.rateLimitBackoffMs);

	Client client(config);
	client.setStop(stopFlag);
	std::string username;
	if (!client.profile(username, error))
	{
		util::fail(error);
		return 1;
	}
	util::info("logged in as " + username);

	if (config.upgradeAccount)
	{
		std::string upgradeError;
		if (client.upgrade(upgradeError))
			util::info("account is a bot account");
		else
			util::warn(upgradeError);
	}

	Engine engine;
	if (!startEngine(engine, makeEngineSettings(config), error))
	{
		util::fail(error);
		return 1;
	}
	util::info("engine ready: " + config.enginePath);
	std::string rematchText = "off";
	if (config.rematch)
	{
		rematchText = "on " + joinList(config.rematchOn, ",");
		rematchText += " (diff " + std::to_string(config.rematchMinRatingDiff);
		if (config.rematchMaxRatingDiff > 0)
			rematchText += ".." + std::to_string(config.rematchMaxRatingDiff);
		rematchText += ", color " + config.rematchColor + ")";
	}
	util::info(std::string("draws: offer ") + (config.drawOffer ? "on" : "off") + ", accept " + (config.drawAccept ? "on" : "off")
	           + " · idle abort " + (config.opponentIdleAbortSeconds > 0 ? std::to_string(config.opponentIdleAbortSeconds) + "s" : "off")
	           + " · claim win " + (config.claimWin ? "on" : "off")
	           + " · rematch " + rematchText);

	Book book;
	if (!config.bookPath.empty())
	{
		if (!book.load(config.bookPath, error))
		{
			util::fail(error);
			return 1;
		}
		util::info("book loaded: " + config.bookPath + " (" + std::to_string(book.size()) + " entries)");
	}

	std::atomic<int> matchState{ IDLE };
	std::thread gameThread;
	std::thread scheduler;
	std::thread acceptTimer;

	const auto startGame = [&](const std::string &gameId, const std::string &colorHint) {
		if (gameThread.joinable())
			gameThread.join();
		gameThread = std::thread([&, gameId, colorHint] {
			GameContext context{ client, engine, book, config, gameId, username, colorHint, stopFlag };
			playGame(context);
			matchState.store(IDLE);
		});
	};

	if (config.challengeBots || !config.challengeUsers.empty())
	{
		if (config.challengeBots && !config.challengeUsers.empty())
			util::warn("challenge_users are ignored while challenge_bots is enabled");
		scheduler = std::thread([&] {
			std::string callError;
			std::mt19937_64 rng(std::random_device{}());
			std::deque<std::string> recent;
			size_t next = 0;
			int sent = 0;

			for (int tick = 0; tick < config.challengeInitialDelaySeconds * 5 && !stopFlag; ++tick)
				util::sleepMs(200);

			while (!stopFlag && (config.challengeCount == 0 || sent < config.challengeCount))
			{
				while (!stopFlag && matchState.load() != IDLE)
					util::sleepMs(200);
				if (stopFlag)
					break;

				std::string opponent;
				if (config.challengeBots)
				{
					std::vector<std::string> bots;
					if (!client.onlineBots(bots, 100, callError))
					{
						util::warn("online bot list failed: " + callError);
					}
					else
					{
						std::vector<std::string> pool;
						for (const std::string &name : bots)
							if (!util::iequals(name, username) && std::find(recent.begin(), recent.end(), name) == recent.end())
								pool.push_back(name);
						if (pool.empty())
						{
							recent.clear();
							for (const std::string &name : bots)
								if (!util::iequals(name, username))
									pool.push_back(name);
						}
						if (pool.empty())
							util::warn("no online bots to challenge");
						else
							opponent = pool[rng() % pool.size()];
					}
				}
				else
				{
					opponent = config.challengeUsers[next % config.challengeUsers.size()];
					++next;
				}

				if (!opponent.empty())
				{
					const std::string variant = pickVariant(config, rng);
					if (client.createChallenge(opponent, challengeForm(config, variant), callError))
					{
						++sent;
						util::info("challenge sent to " + opponent + " (" + variant + ")");
						recent.push_back(opponent);
						if (recent.size() > 16)
							recent.pop_front();
					}
					else
						util::warn("challenge to " + opponent + " failed: " + callError);
				}

				for (int tick = 0; tick < config.challengeIntervalSeconds * 5 && !stopFlag; ++tick)
					util::sleepMs(200);
			}
		});
	}

	while (!stopFlag)
	{
		int status = 0;
		client.eventStream([&](const json::Value &event) {
			std::string callError;
			const std::string type = std::string(json::string(event, "type"));

			if (type == "challenge")
			{
				const json::Value *challenge = event.find("challenge");
				if (!challenge)
					return true;
				const ChallengeInfo info = parseChallenge(*challenge);
				std::string reason;
				if (!acceptsChallenge(info, config, reason))
				{
					util::info("declining challenge from " + info.challenger + " (" + reason + ")");
					client.declineChallenge(info.id, reason, callError);
					return true;
				}

				int expected = IDLE;
				if (!matchState.compare_exchange_strong(expected, STARTING))
				{
					util::info("busy playing, declining challenge from " + info.challenger);
					client.declineChallenge(info.id, "later", callError);
					return true;
				}

				if (client.acceptChallenge(info.id, callError))
				{
					util::info("accepted challenge from " + info.challenger + " (" + info.variant + ")");
					if (acceptTimer.joinable())
						acceptTimer.join();
					acceptTimer = std::thread([&] {
						for (int tick = 0; tick < 300 && !stopFlag; ++tick)
						{
							util::sleepMs(100);
							if (matchState.load() != STARTING)
								return;
						}
						int expected = STARTING;
						if (matchState.compare_exchange_strong(expected, IDLE))
							util::warn("no game started after accepting a challenge");
					});
				}
				else
				{
					util::warn("accept failed: " + callError);
					matchState.store(IDLE);
				}
			}
			else if (type == "gameStart")
			{
				const json::Value *game = event.find("game");
				const std::string gameId = game ? std::string(json::string(*game, "id")) : std::string();
				const std::string colorHint = game ? std::string(json::string(*game, "color")) : std::string();
				if (gameId.empty())
					return true;

				const int state = matchState.load();
				if (state == PLAYING)
				{
					util::warn("game " + gameId + " started while busy, resigning");
					client.resign(gameId, callError);
					return true;
				}

				matchState.store(PLAYING);
				util::info("game starting: " + gameId);
				startGame(gameId, colorHint);
			}
			else if (type == "gameFinish")
			{
				const json::Value *game = event.find("game");
				if (game)
					util::info("game finished: " + std::string(json::string(*game, "id")));
			}
			else if (type == "challengeDeclined")
				util::info("challenge declined by opponent");
			else if (!type.empty())
				util::info("event: " + type);

			return !stopFlag.load();
		}, stopFlag, status, error);

		if (stopFlag)
			break;
		if (status == 401 || status == 403)
		{
			util::fail("event stream rejected with " + std::to_string(status) + ", check the token");
			break;
		}
		util::warn("event stream ended (" + (error.empty() ? "http " + std::to_string(status) : error) + "), reconnecting");
		for (int tick = 0; tick < 25 && !stopFlag; ++tick)
			util::sleepMs(200);
	}

	util::info("shutting down");
	if (scheduler.joinable())
		scheduler.join();
	if (gameThread.joinable())
		gameThread.join();
	if (acceptTimer.joinable())
		acceptTimer.join();
	engine.stop();
	util::statusClear();
	return 0;
}
