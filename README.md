# Regresja symboliczna w C++23

Implementacja etapów **19.10 i 26.10**: środowisko, dane fizyczne, próba PySR,
szkic raportu oraz obliczanie zadanych wyrażeń. Wyszukiwanie ewolucyjne w C++,
optymalizacja stałych i szablony pozostają kolejnymi etapami planu.

## Struktura i odpowiedzialności

| Ścieżka | Odpowiedzialność |
|---|---|
| `include/sr/dataset.hpp`, `src/dataset.cpp` | Dane tabelaryczne, walidacja, odczyt i zapis CSV |
| `include/sr/expression.hpp`, `src/expression.cpp` | Drzewa wyrażeń, rejestr operatorów/loss, obliczanie, predykcja i strata |
| `include/sr/benchmarks.hpp`, `src/benchmarks.cpp` | Definicje trzech zależności fizycznych, próbkowanie i szum |
| `apps/generate_data.cpp` | Generator CSV i manifestu; parametry w `data/SPEC.md` |
| `apps/example.cpp` | Przykład własnych operatorów unarnych/binarnych i funkcji straty |
| `tests/test_sr.cpp` | Wartości wzorcowe, dziedziny, błędne CSV i powtarzalność danych |
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
```

Przykład oblicza **zadany** wzór sprężyny, nie odkrywa go automatycznie.
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
Kod użycia znajduje się w `apps/example.cpp`; parser tekstowych wzorów nie jest
potrzebny do tego etapu. `to_string` służy do wyświetlania, nie serializacji modelu.

Standardowe operatory: `+`, `-`, `*`, `/`, `sin`, `cos`, `square`; straty: `mse`,
`mae`. Puste i powtórzone rejestracje są odrzucane. Niepoprawne dane, nazwy lub
rozmiary powodują `std::invalid_argument`; niepoprawna arytmetyka, przepełnienie
i ujemna/nieskończona strata powodują `std::domain_error`. Wyszukiwanie będzie
mogło później odrzucać takie wyrażenia zamiast uznawać je za dobre rozwiązania.

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
Próba wbudowanej kompilacji jest obecnie zablokowana: kompilator nie ma w pamięci
podręcznej pakietu TeX i nie może pobrać `default_bundle_v33.tar.index.gz` z
`relay.fullyjustified.net`. Kompilacja źródła nie została jeszcze potwierdzona.

Dokumentacja źródłowa: [PySR API](https://pysr.ai/v2.6.0/api),
[PySR / SymbolicRegression.jl](https://github.com/astroautomata/PySR).
