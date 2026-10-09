# Lichess.org Bridge for UCI Chess Engines

Minimal C++ wrapper that plays Lichess games with a UCI engine.

No runtime dependencies beyond OpenSSL (`libssl`, `libcrypto`), which is used
for HTTPS. JSON, chunked transfer decoding, NDJSON streaming, Polyglot hashing
and the board representation are all implemented.

## Build

Requires a C++23 compiler.

```
make
```

### Windows cross-compile

The bridge builds for 64-bit Windows from Linux with MinGW-w64.

```
make windows
```

This produces exe. Point the build at another toolchain or OpenSSL
prefix if needed:

```
make windows WINDOWS_TRIPLE=i686-w64-mingw32
make windows WINDOWS_PREFIX=/opt/mingw
```

## Run

```
./bridge <path-to-config>
```


## Config keys

| Key | Default | Meaning |
| --- | --- | --- |
| `token` | empty | Lichess bot API token |
| `base_url` | `https://lichess.org` | API root, override for tests |
| `engine` | empty | path to the UCI engine binary |
| `engine_net` | empty | sent as `EvalFile` when set |
| `engine_hash` | 16 | `Hash` option, MiB |
| `engine_threads` | 1 | `Threads` option |
| `engine_option` | empty | extra `setoption`, repeatable, `Name Value` |
| `move_overhead` | 150 | milliseconds kept back from the clock |
| `correspondence_movetime` | 10000 | movetime for unclocked/correspondence games |
| `book` | empty | `.bin` (Polyglot) or `.epd` book |
| `book_depth` | 0 | plies for which the book is consulted |
| `chat_eval` | true | post occasional human-like remarks to the player and spectator chat |
| `chat_edge_cp` | 60 | score (cp) for the slight-edge remark |
| `chat_edge_moves` | 3 | consecutive searches at `chat_edge_cp` before the slight-edge remark |
| `chat_win_cp` | 120 | score (cp) for the clear-advantage remark |
| `chat_win_moves` | 2 | consecutive searches at `chat_win_cp` before the clear-advantage remark |
| `chat_crush_cp` | 500 | score (cp) for the winning remark |
| `chat_crush_moves` | 1 | consecutive searches at `chat_crush_cp` before the winning remark |
| `chat_draw_cp` | 25 | score band (cp) that counts as balanced for chat |
| `chat_draw_moves` | 6 | consecutive balanced searches before the draw remark |
| `chat_draw_min_ply` | 20 | minimum ply before the draw remark |
| `chat_mate_close` | 3 | mate distance for the "mate is closing in" remark, 0 disables |
| `chat_command` | empty | user chat reply, repeatable: `trigger : condition : response` |
| `draw_offer` | true | offer a draw once the position looks dead drawn |
| `draw_offer_cp` | 15 | score band (cp) that counts as a dead draw |
| `draw_offer_moves` | 4 | consecutive searches in the band before offering |
| `draw_offer_min_ply` | 20 | minimum ply before a draw may be offered |
| `draw_accept` | true | accept the opponent's draw offer in a dead drawn position |
| `draw_accept_cp` | 30 | score band (cp) that accepts an incoming draw offer |
| `opponent_idle_abort` | 60 | seconds of opponent inactivity before aborting, 0 disables |
| `claim_win` | true | claim the win once the server's abandonment timer runs out |
| `rematch` | true | offer a rematch challenge after the game when the rules below match |
| `rematch_on` | `win,lose,draw` | outcomes that allow a rematch, comma separated |
| `rematch_min_rating_diff` | 1 | only rematch when the opponent is at least this many points below us |
| `rematch_max_rating_diff` | 0 | cap on how far below us the opponent may be, 0 disables the cap |
| `rematch_color` | `random` | colour for the rematch challenge: `random`, `same` or `opposite` |
| `accept_variants` | `standard` | comma separated variants |
| `accept_rated` | true | accept rated challenges |
| `accept_casual` | true | accept casual challenges |
| `accept_correspondence` | false | accept correspondence challenges |
| `accept_from` | empty | comma separated whitelist of challengers |
| `accept_min_seconds` | 0 | minimum initial clock, seconds |
| `accept_max_seconds` | 0 | maximum initial clock, 0 disables the cap |
| `accept_min_increment` | 0 | minimum increment, seconds, 0 disables the filter |
| `challenge_users` | empty | bot accounts to challenge, comma separated (ignored while `challenge_bots` is on) |
| `challenge_bots` | false | discover online bots and challenge a random one |
| `challenge_variants` | empty | variants drawn from per outgoing challenge, `standard,chess960` |
| `challenge_interval` | 60 | seconds between outgoing challenges |
| `challenge_initial_delay` | 30 | seconds before the first challenge |
| `challenge_limit` | 180 | outgoing initial clock, seconds |
| `challenge_increment` | 2 | outgoing increment, seconds |
| `challenge_rated` | true | outgoing challenge rated flag |
| `challenge_variant` | `standard` | outgoing variant |
| `challenge_color` | `random` | outgoing colour |
| `challenge_count` | 0 | stop after this many challenges, 0 loops forever |
| `upgrade_account` | true | call `/api/bot/account/upgrade` on start |
| `request_interval` | 100 | minimum milliseconds between API requests |
| `rate_limit_backoff` | 60000 | milliseconds to pause after a 429 that has no `Retry-After` |

Unknown keys are a startup error.

## Chat commands

Besides the built-in `position?` report and the automatic remarks, the bot can
answer chat with commands you define. `chat_command` is repeatable and takes
three fields separated by `:`:

```
chat_command = <trigger> : <condition> : <response>
```

* **trigger** — the message text to answer, matched case-insensitively and in
  full. `*` answers every message. A configured trigger overrides the built-in
  `position?`/`position` report.
* **condition** — a boolean expression that must hold before the reply is sent;
  empty means always. It combines `&&`, `||`, `!` and parentheses. Terms are
  `always`, `our_turn`, `opponent_turn`, `rated`, `casual`, `scored`,
  `has_mate` and `getting_mated`, plus `eval`, `mate`, `depth`, `ply` and
  `clock` compared with `>`, `>=`, `<`, `<=`, `==`, `!=` against a number
  (e.g. `eval >= 120`, `clock <= 30`). `eval` is our score in centipawns and is
  false until the engine has searched.
* **response** — the text to send, with `{placeholders}` filled in. Separate
  alternatives with `|` to have one picked at random.

Placeholders: `{eval}` `{cp}` `{mate}` `{depth}` `{seldepth}` `{nodes}` `{nps}`
`{hash}` `{time}` `{clock}` `{best}` `{turn}` `{ply}` `{last_move}` `{us}`
`{opponent}` `{our_rating}` `{opponent_rating}` `{rated}` `{result}` `{report}`.
`{eval}` is our score, `{time}` is the engine's search time, `{clock}` is our
remaining time in seconds, `{ply}` is the number of half moves played and
`{report}` is the full search report used by `position?`. Write `{{` and `}}`
for literal braces. Unknown triggers, conditions and placeholders are a startup
error.

```
chat_command = position? : always : {report}
chat_command = !eval : always : eval {eval} at depth {depth}
chat_command = !clock : our_turn : I have {clock}s left
chat_command = gg : always : good game|well played|thanks for the game
chat_command = !edge : eval >= 100 : I think I have a small edge here
```

Replies share the `position?` cooldown, so a burst of triggers is answered at
most once every two seconds.

## Books

`.bin` files are read as Polyglot books, `.epd` files as positions with `bm`
and `pv` move lists. Entries are matched on the full Polyglot key (including
castling, en passant and side to move), disabled entries (weight 0) are
skipped, and a move is drawn at random weighted by the entry weight. The book
is consulted while the played move count is below `book_depth`; anything the
book does not answer is searched by the engine.

## Reconnection

The game stream is reconnected for as long as the game is not over with a
backoff that grows to 30 seconds, so an outage of any length is ridden out and
play resumes by itself once the link is back. The stream replays the whole game
on every connect and the bridge re-syncs from that state. If the move that was
in flight when the link died never reached the server, it is searched and played
again. Connections use TCP keepalive (15s idle, 5s interval, 3 probes) so an
idle stream notices a dead link on its own instead of waiting for the next
write.

## Rematch

With `rematch = true`, a finished game is followed by a challenge to the same
opponent when the configured rules match. `rematch_on` selects which outcomes
qualify (`win`, `lose`, `draw`), `rematch_min_rating_diff` and
`rematch_max_rating_diff` bound how much weaker the opponent must be (the
difference is our rating minus theirs, so a positive value means the opponent
is weaker; `rematch_max_rating_diff` of 0 disables the upper bound), and
`rematch_color` picks the colour for the new challenge (`random`, `same` or
`opposite`). Correspondence games and games that never started are never
rematched.

## License

See `LICENSE`.