# Othello in Dyalog APL

An Othello engine written in Dyalog APL (`⎕IO←0`). The engine is the scripted namespace `src/Othello.apln`; this document outlines the algorithms it implements.

## Layout

| File | Contents |
| --- | --- |
| `src/Othello.apln` | The engine: board, move generation, evaluation, search |
| `src/Games.apln` | Square names, text pictures, the game loop, agents |
| `src/Harness.apln` | Match harness: opening sets, colour-swapped pairs, statistics |
| `src/Tests.apln` | Tests, a naive reference implementation, benchmarks |
| `src/data.dcf` | Component file with opening sets and test positions |
| `ui/othello.apls` | A raylibAPL front end |

## Board representation

A board is a `2 64` boolean array: row 0 holds the discs of the side to move, row 1 those of the opponent. Square `i` is `8⊥rank file`, so a1 is 0 and h8 is 63. Because the board is relative to the side to move, passing the turn is `⊖`, and `Play` returns the board from the opponent's point of view.

All board functions are rank-polymorphic: a stack of boards has shape `n 2 64`, and moves, play, terminal tests and scores are computed for every board in the stack at once.

## Move generation

The eight directions are a matrix of `(Δrank Δfile)` pairs. Each direction becomes a rotation of the 64-vector (`rot`) together with a mask of the squares a one-step shift can reach without wrapping round an edge (`mask`). `S` applies all eight shifts at once to an `(…,8,64)` array.

- `Run` starts from one set of squares, steps once in every direction, keeps opponent discs, and repeats (`⍣≡`) until the runs stop growing. The result is, per direction, the contiguous opponent discs reachable from the start.
- `Moves` runs from the side to move's discs; one more step from each run that lands on an empty square is a legal move. The eight directions are combined with `∨⌿`.
- `Flips` runs from the played square and keeps only the runs whose next step lands on one of the mover's own discs.
- `Play` adds the played square and the flipped discs to the mover, removes the flipped discs from the opponent, and swaps the rows.
- `Terminal` holds when neither side has a move.

`Perft` counts leaf nodes a whole ply at a time: it plays every legal move of every board in the stack in a single call. A forced pass counts as a ply, and a finished game counts as one leaf at any remaining depth.

## Evaluation

Scores are in hundredths of a disc. A finished game scores `100×Score`, where `Score` is the disc difference with empty squares awarded to the winner. `Eval` returns a heuristic within ±6400 for each board in a stack, using integer arithmetic throughout (`Div` truncates toward zero). It sums four terms:

| Term | Measure |
| --- | --- |
| Square table | Fixed weight per square (`weights`), own discs minus the opponent's |
| Mobility | `1000×(a−t)÷(a+t+1)` for the move counts `a` and `t` of the two sides |
| Frontier | Opponent discs minus own discs next to an empty square |
| Corner risk | Discs on X-squares (49) and C-squares (19) while the adjacent corner is empty |

The mobility, frontier and corner-risk weights are interpolated linearly between opening and endgame values, according to the number of empty squares (from 60 down to 12).

## Search

`Search` is a fail-soft negamax with alpha-beta pruning. Its left argument is depth, alpha, beta and an optional transposition table.

- Finished games and forced passes are resolved before the depth cutoff, and a pass costs no depth.
- All children of a node are generated with one `Play` call. `_Leaf` gives each child its depth-0 value in one batched call: the evaluation, the exact score for a finished game, or the negated evaluation of the opponent's position after a forced pass.
- At depth 1 the node's value is the maximum of those child values, so the last ply needs no recursion.
- At deeper nodes children are searched in order of their depth-0 values, best first, with the move stored in the transposition table tried before the rest.

### Transposition table

`New n` makes a table with `2*n` slots. A position's key is the sum of 128 random 45-bit numbers (`zobrist`), one for each set bit of the board, which stays exact below `2*53`. The slot is the key modulo the table size, entries are always replaced, and the full key is compared on lookup.

Each entry records the value, the depth, the bound type (exact, lower or upper, judged against the window the node was searched with) and the best move. An entry produces a cutoff only when it was stored at the same depth. Otherwise its move is still tried first.

The table also carries the search limits: a deadline and a node limit. When either is reached, `Search` signals and unwinds without storing anything for the unfinished nodes.

### Iterative deepening and the endgame

`ms [depth [nodes [threshold]]] Think board` searches with increasing depth until the time or node limit, returning the square, value, depth, node count, a status (complete, partial or fallback) and whether the position was solved. If a limit interrupts an iteration, the result of the last completed iteration is used.

Depth never exceeds the number of empty squares. Since passes cost no depth, a search to that depth reaches only finished games, so the last iteration is an exact solve. It runs in two steps sharing the table: a narrow window `(¯1 1)` decides win, draw or loss, then a window chosen by that result finds the exact margin. With at most `threshold` empty squares (default 12), the search deepens only to depth 4 before solving, so a sound move is available if the solve does not finish in time.

## Usage

Link the namespaces into a session (engine first), or `2⎕FIX` the files in a script:

```apl
]link.create Othello src/Othello.apln
]link.create Games   src/Games.apln
Othello.(3000 Think start)        ⍝ square value depth nodes status solved
Games.(Show(Human _Game_ Rnd)0 eng.start)
```

Run `Tests.RunTests 0` for the tests, `./tools/match.apls` for a headless match, and `./ui/othello.apls` for the graphical front end.
