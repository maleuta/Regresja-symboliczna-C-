# Regresja symboliczna w C++23

Implementacja etapów **19.10–23.11**: środowisko, dane fizyczne, próba PySR,
szkic raportu, obliczanie wyrażeń, wyszukiwanie ewolucyjne z optymalizacją stałych,
hall of fame, własne operatory i funkcje straty w wyszukiwaniu oraz ograniczenia
złożoności. Szablony wyrażeń i porównanie z PySR są kolejnymi etapami planu.
Szczegółowy opis kodu i decyzji projektowych (po angielsku):
[project_explanation.md](project_explanation.md).

## Struktura i odpowiedzialności

| Ścieżka | Odpowiedzialność |
|---|---|
| `include/sr/dataset.hpp`, `src/dataset.cpp` | Dane tabelaryczne, walidacja, odczyt i zapis CSV |
| `include/sr/expression.hpp`, `src/expression.cpp` | Drzewa wyrażeń, rejestr operatorów/loss, obliczanie, predykcja i strata |
| `include/sr/benchmarks.hpp`, `src/benchmarks.cpp` | Definicje trzech zależności fizycznych, próbkowanie i szum |
| `include/sr/constraints.hpp`, `src/constraints.cpp` | Limity rozmiaru, głębokości, argumentów operatorów i wykładnika potęgi |
| `include/sr/search.hpp`, `src/search.cpp` | Konfiguracja, operatory wariacji, optymalizacja stałych, `fit` |
| `apps/generate_data.cpp` | Generator CSV i manifestu; parametry w `data/SPEC.md` |
| `apps/example.cpp` | Obliczanie wzoru sprężyny, wydruk drzewa oraz błędy MSE i MAE |
| `apps/custom_functions.cpp` | Własny operator unarny, binarny i strata Huber, także w wyszukiwaniu |
| `apps/constraints.cpp` | Sprawdzanie reguł i wyszukiwanie z ograniczeniami (projectile) |
| `apps/search.cpp` | Program `sr_search`: dopasowanie, wybór na walidacji, test, eksport |
| `tests/test_sr.cpp` | Wartości wzorcowe, dziedziny, błędne CSV i powtarzalność danych |
| `tests/test_search.cpp` | Edycja drzew, `pow`, ograniczenia, mutacje, optymalizacja, wyszukiwanie |
| `baseline/` | Wyłącznie adapter do uruchamiania PySR i przypięte zależności Pythona |
| `report/report.tex` | Samodzielny szkic artykułu LaTeX |

Biblioteka i generator nie wymagają zewnętrznych zależności C++.

## Budowanie i testy

Wymagane są CMake ≥3.20 i kompilator obsługujący C++23. Sprawdzono GCC 15.2.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 2
ctest --test-dir build --output-on-failure
build/sr_example
build/sr_generate artifacts/data
build/sr_example artifacts/data/spring_clean_test.csv
build/sr_custom artifacts/data/projectile_noisy_test.csv
build/sr_constraints
build/sr_search
build/sr_search artifacts/data/projectile_clean_{train,validation,test}.csv --iterations 200 --population 100 --output artifacts/search/projectile
```

`sr_example` oblicza **zadany** wzór sprężyny; wzory odkrywa `sr_search`.
Programy zwracają kod 1 i komunikat w przypadku błędu.

Opcjonalne sprawdzenie pamięci (GCC/Clang):

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DSR_SANITIZERS=ON
cmake --build build-sanitize -j 2
ctest --test-dir build-sanitize --output-on-failure
```

LeakSanitizer nie działa pod `ptrace`. W takim środowisku można wykonać testy
poza śledzonym sandboxem albo użyć `ASAN_OPTIONS=detect_leaks=0`, co zachowuje
ASan/UBSan, ale wyłącza kontrolę wycieków. Pełny wariant sprawdzono poza sandboxem.

## Kontrakt CSV i obliczeń

CSV to nagłówek i skończone liczby, bez cudzysłowów i brakujących wartości.
Nazwy mają postać `[A-Za-z_][A-Za-z0-9_]*`, muszą być unikalne; domyślnym
celem jest `y` (może wystąpić w dowolnej kolumnie). Obsługiwane są CRLF,
spacje wokół pól i notacja naukowa. Puste wiersze, NaN/Inf, nadmiarowe kolumny,
błędne liczby oraz brak celu są odrzucane. Zapis umieszcza cel na końcu.

`Expression::constant/variable/unary/binary` budują drzewa o poprawnej arności.
`FunctionRegistry::add_unary/add_binary/add_loss` rejestrują funkcje C++.
`evaluate` oblicza pojedynczy wiersz według nazw zmiennych; `predict` sprawdza
i oblicza cały `Dataset`; `mean_loss` uśrednia nieujemną stratę punktową.
`size` liczy każdy węzeł jako 1, a `depth` przyjmuje głębokość liścia równą 1.
Kod użycia znajduje się w `apps/example.cpp` (operatory standardowe) i
`apps/custom_functions.cpp` (własne `exp`, `hypot` i strata Huber, również w
wyszukiwaniu); parser tekstowych wzorów nie jest potrzebny. `to_string` służy do wyświetlania, nie serializacji modelu.

`to_tree()` zwraca tekstowe drzewo z typami węzłów i gałęziami ASCII. Nie wymaga
rejestru funkcji ani danych i nie wykonuje obliczeń. Przykład `build/sr_example`
pokazuje wzór `2.5 * x + 0.75` zbudowany z operatorów `*` i `+`, jego drzewo
oraz błędy MSE i MAE.
Drzewo można wypisać w dowolnym miejscu, również później podczas wyszukiwania:

```cpp
std::cout << formula.to_tree();
```

Dla drzewa odpowiadającego `2.5 * x + 0.75` wynik wygląda tak:

```text
binary: +
|-- binary: *
|   |-- constant: 2.5
|   `-- variable: x
`-- constant: 0.75
```

`unary` oznacza funkcję z jednym argumentem, `binary` z dwoma, `constant` stałą,
a `variable` zmienną. Dzieci funkcji binarnej są wyświetlane w kolejności lewy,
prawy argument. Metoda
wyświetla już istniejący `Expression`; wprowadzanie wzoru jako tekstu wymagałoby parsera.

Standardowe operatory: `+`, `-`, `*`, `/`, `pow`, `sin`, `cos`, `square`; straty:
`mse`, `mae`. `pow` jest zdefiniowany tylko dla rzeczywistych wyników: `0^e` dla
`e < 0` oraz ujemna podstawa z niecałkowitym wykładnikiem dają `std::domain_error`.
Puste i powtórzone rejestracje są odrzucane. Niepoprawne dane, nazwy lub
rozmiary powodują `std::invalid_argument`; niepoprawna arytmetyka, przepełnienie
i ujemna/nieskończona strata powodują `std::domain_error`. Wyszukiwanie przypisuje
takim kandydatom stratę +∞, więc nie trafiają do hall of fame.

`subtree(i)` i `replaced(i, drzewo)` adresują węzły w kolejności preorder (korzeń
ma indeks 0); edycja zwraca nowe drzewo i nie zmienia oryginału. `constants()` oraz
`with_constants(...)` udostępniają stałe w tej samej kolejności. `predict` kompiluje
drzewo raz do programu postfiksowego i daje te same wyniki co `evaluate`.

## Wyszukiwanie

`sr::fit(train, validation, options, functions)` wykonuje ewolucję z jedną populacją:
losową inicjalizację drzew dopuszczalnych, selekcję turniejową, sześć mutacji
i krzyżowanie poddrzew, zastępowanie najstarszego osobnika, optymalizację stałych
przez przeszukiwanie kolejnych współrzędnych (z krokiem parabolicznym) oraz hall
of fame z najlepszym wzorem dla każdej złożoności. Model jest wybierany na danych
walidacyjnych: najprostszy wzór frontu Pareta o stracie co najwyżej 5% większej
od minimalnej. Dla ustalonego ziarna i bez limitu czasu wynik jest powtarzalny.

```cpp
sr::FunctionRegistry functions;
functions.add_binary("hypot", [](double a, double b) { return std::hypot(a, b); });
sr::SearchOptions options;
options.binary_operators = {"+", "-", "*", "hypot"};  // nazwy z rejestru
options.loss = "mse";
options.population_size = 64;
options.iterations = 40;
options.seed = 0;
options.time_limit_seconds = 90;                      // opcjonalnie; także target_loss
options.constraints.max_size = 20;
options.constraints.max_depth = 10;
options.constraints.arguments["pow"] = {3, 1};        // jak PySR constraints={"pow": (3, 1)}
options.constraints.exponent = sr::ExponentLimit{.max_abs_value = 3, .integer_only = true};
const auto result = sr::fit(train, validation, options, functions);
// result.best, result.hall_of_fame, result.population, result.statistics
```

Własne operatory i funkcje straty są przekazywane nazwą, więc pętla wyszukiwania
nie wymaga zmian. Złożoność to liczba węzłów. Ograniczenia obejmują `max_size`,
`max_depth`, maksymalny rozmiar każdego argumentu operatora oraz wykładnik potęgi
(stała, `|e| ≤ k`, opcjonalnie całkowita). Losowe drzewa są budowane w granicach
limitów, a wynik każdej mutacji i krzyżowania jest sprawdzany przez `violation()`;
niedopuszczalna propozycja jest odrzucana i losowana ponownie. Niepoprawna
konfiguracja (nieznany operator, zła arność, limit dla operatora spoza zbioru
itp.) powoduje `std::invalid_argument` przed startem.

`sr_search --help` opisuje opcje programu. `--output DIR` zapisuje
`hall_of_fame.csv` oraz `result.json` z konfiguracją, statystykami, wzorem
i błędami (nazwy pól zgodne z adapterem PySR).

## Próba PySR

Wymagany Python 3.12 i dostęp do internetu przy pierwszej instalacji. `uv`
służy wyłącznie do przygotowania lokalnego środowiska:

```sh
UV_CACHE_DIR=.cache/uv uv venv --python 3.12 .venv
UV_CACHE_DIR=.cache/uv uv pip sync --python .venv/bin/python baseline/requirements.lock
build/sr_generate artifacts/data
.venv/bin/python baseline/run_pysr.py --problem spring --variant clean
```

Adapter zapisuje Julię i jej zależności w `.tools/`, ustawia jeden wątek i używa
Float64, jednej populacji oraz stałego ziarna. Przed właściwą próbą wykonuje
osobne rozgrzewanie. `search_fit_s` wyklucza import i rozgrzewanie, ale obejmuje
narzut wywołania `fit`; nie jest czystym czasem jądra wyszukiwania.

Wyniki w `artifacts/pysr-pilot/`: `result.json` (wzór, błąd, złożoność, wersje,
konfiguracja i skróty danych), `equations.csv`, `predictions.csv` oraz rzeczywiste
Julia `Project.toml`/`Manifest.toml`. Wzór jest wybierany według błędu walidacyjnego,
następnie oceniany na testowym. Opcje `--problem projectile` / `--problem oscillator`
i `--variant noisy` pozwalają sprawdzić pozostałe dane; dla kolejnej próby należy
podać osobny `--output`. To pilotaż środowiska, nie końcowe porównanie 36 uruchomień.

## Raport

`report/report.tex` zawiera cel, przegląd literatury, metody, dane, sekcje wyników,
dyskusji i wniosków. Opisuje aktualny stan bez przypisywania implementacji C++
nieuzyskanych wyników wyszukiwania. Dokument można otworzyć w edytorze LaTeX Codex.
Do kompilacji poza edytorem dostępny jest lokalny
[Tectonic 0.17.0](https://github.com/tectonic-typesetting/tectonic/releases/tag/tectonic%400.17.0)
w `.tools/tectonic/tectonic`. Plik binarny pochodzi z oficjalnego wydania
`x86_64-unknown-linux-musl`; jego SHA-256 zweryfikowano względem metadanych wydania.

```sh
mkdir -p artifacts/report
XDG_CACHE_HOME="$PWD/.cache" .tools/tectonic/tectonic --untrusted --keep-logs -o artifacts/report report/report.tex
```

Wynik to `artifacts/report/report.pdf`; log kompilacji znajduje się w tym samym
katalogu. Przy pierwszym uruchomieniu pobierane są pakiety TeX, przechowywane
w `.cache/`. Kompilator, pakiety i wygenerowane pliki nie są zapisywane w Git.
Kompilację raportu i poprawność polskich znaków potwierdzono.
Po pobraniu pakietów działa również kompilacja offline z opcją `--only-cached`.

Dokumentacja źródłowa: [PySR API](https://pysr.ai/v2.6.0/api),
[PySR / SymbolicRegression.jl](https://github.com/astroautomata/PySR).
