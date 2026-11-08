#pragma once

#include <atomic>
#include <string>

class Book;
class Client;
class Engine;
struct Config;

struct GameContext
{
	Client &client;
	Engine &engine;
	const Book &book;
	const Config &config;
	std::string gameId;
	std::string username;
	std::string colorHint;
	const std::atomic<bool> &stop;
	const std::atomic<bool> &drain;
};

void playGame(GameContext &context);
