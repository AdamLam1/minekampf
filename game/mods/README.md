# Minekampf — mody

Wrzuć pliki `*.json` do tego katalogu. Każdy plik to **tablica receptur**
nadpisujących wbudowane (receptura o tym samym przedmiocie wyjściowym zastępuje
oryginał). Gra ładuje je przy starcie; błędne pliki są pomijane z wpisem w logu.

## Format

```json
[
  {
    "type": "shaped",
    "output": "stick",
    "count": 9,
    "pattern": ["X", "X"],
    "ingredients": {"X": "oak_planks"}
  },
  {
    "type": "shapeless",
    "output": "glass",
    "count": 2,
    "items": ["sand", "coal"]
  }
]
```

- `output` — nazwa przedmiotu/bloku z rejestru (nieczuła na wielkość liter;
  bloki: `stone`, `crafting_table`, `furnace`, `quest_npc`...; przedmioty:
  `stick`, `coal`, `apple`, `raw_meat`, `cooked_meat`, `charcoal`, `bow`,
  `arrow`, `iron_ingot`, `diamond`...), `count` 1–64
- `shaped` — `pattern` 1–3 wierszy po ≤3 znaki, `ingredients` mapuje znak → nazwa
- `shapeless` — `items` 1–9 nazw, kolejność dowolna

Zawartość `example_recipes.json` to działający przykład: patyki 9 sztuk
i szkło z piachu + węgla. Usuń plik, żeby wrócić do wbudowanych przepisów.
