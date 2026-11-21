# Prezentacja 23.11.2026 — konfigurowalność i ograniczenia

| Plik | Zawartość |
|---|---|
| `slides.tex` | Slajdy Beamer (17 slajdów, 16:9) |
| `visualizer.html` | Wizualizacja przebiegu wyszukiwania; otwiera się bez internetu |
| `traces.js`, `data/*.csv` | Zapis 5 rzeczywistych uruchomień `sr_search` (wizualizacja, wykresy na slajdach) |
| `make_traces.sh` | Odtwarza `traces.js` i `data/` z uruchomień programu |

## Przygotowanie (dzień przed)

```sh
# kompilacja Release: pokaz musi działać szybko (Debug: 38 s zamiast 1,7 s)
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release
cmake --build build-rel -j 4
build-rel/sr_generate artifacts/data

# opcjonalnie: odświeżenie danych wizualizacji i wykresów (wyniki są deterministyczne)
presentation/make_traces.sh

# slajdy -> artifacts/presentation/slides.pdf (pierwsza kompilacja pobiera pakiety TeX)
mkdir -p artifacts/presentation
XDG_CACHE_HOME="$PWD/.cache" .tools/tectonic/tectonic --untrusted -o artifacts/presentation presentation/slides.tex
```

Kontrola: `ctest --test-dir build-rel` (7/7) i otwarcie `presentation/visualizer.html`
w przeglądarce (Firefox/Chrome, plik lokalny). Jeżeli przeglądarka blokuje skrypty z pliku,
można uruchomić `python3 -m http.server 8765 --directory presentation` i otworzyć
`http://localhost:8765/visualizer.html`. Warto mieć PDF slajdów na pendrivie.

## Scenariusz (ok. 18 minut)

| Czas | Slajd | Co powiedzieć |
|---|---|---|
| 0:00 | 1 Tytuł | Temat etapu: konfigurowalność i ograniczenia. Dziś pokażę też działające wyszukiwanie. |
| 0:30 | 2 Harmonogram | Etapy 09.11 i 16.11 były opóźnione; zrealizowałem je razem z 23.11, bo bez pętli wyszukiwania nie da się pokazać, że ograniczenia działają **w wyszukiwaniu**. Plan od 30.11 bez zmian. |
| 1:30 | 3 Problem | Na wejściu tabela, na wyjściu wzór. Szukamy struktury i stałych jednocześnie; wynikiem jest zestaw wzorów o różnej złożoności. |
| 2:30 | 4 Drzewo | Węzeł zna tylko **nazwę** operatora, a funkcję daje rejestr. To jest klucz do własnych operatorów. Złożoność = liczba węzłów, jak w PySR. |
| 3:30 | 5 Cykl | Turniej, wariacja, sprawdzenie ograniczeń, ocena, zastąpienie **najstarszego**. Starzenie się populacji utrzymuje różnorodność. Błąd arytmetyczny daje stratę ∞. Wynik jest powtarzalny dla ustalonego ziarna. |
| 5:00 | 6 Mutacje | Sześć mutacji i krzyżowanie. Przykład z prawdziwego uruchomienia: poddrzewo `1,57·v0` zastąpione przez `v0`. Potomek bywa gorszy i to jest w porządku. |
| 6:00 | 7 Stałe | Przeszukiwanie współrzędnych zgodnie z planem, z krokiem do wierzchołka paraboli. Dla stałej liniowej przy MSE trafia dokładnie w jednym kroku. |
| 7:00 | 8 Front Pareta | Najlepszy wzór dla każdej złożoności. Wybieramy na walidacji najprostszy wzór w granicy 5% od minimum; samo minimum wybierało zbyt złożone wzory. |
| 8:00 | 9 Własne funkcje | Rejestruję `hypot` i stratę Hubera, a pętla wyszukiwania się nie zmienia. Program znajduje `hypot(Fy, Fx)` dla wypadkowej sił. |
| 9:00 | 10 Ograniczenia | Rozmiar, głębokość, rozmiar argumentów (jak `constraints` w PySR) i wykładnik potęgi. `violation()` mówi, która reguła jest naruszona. |
| 10:00 | 11 Egzekwowanie | Trzy poziomy: generator, filtr z ponownym losowaniem, optymalizator. 1144 propozycje odrzucone, 69/69 dopuszczalnych. |
| 11:00 | 12 Testy | Każda reguła testowana dokładnie na granicy i krok za nią; test własności na 4000 krokach; testy także z sanitizerami. |
| 12:00 | 13 Wyniki | Wstępne, budżet 1–4 s. Sprężyna 6/6, reszta częściowo. Porównanie z PySR dopiero 07.12, z budżetem 90 s. |
| 13:00 | 14 Udany vs nieudany | Mówimy też o porażkach: lokalne optimum przy jednej populacji, składnik `1/t`, który psuje test, brak upraszczania wzorów. |
| 14:00 | 15 Pokaz | Pokaz na żywo, niżej. |
| 17:30 | 16 Dalsze prace | 30.11 szablon i zamrożenie; 07.12 porównanie. |
| 18:00 | 17 Pytania | |

## Pokaz na żywo (ok. 3,5 minuty)

Terminal w katalogu repozytorium, duża czcionka. Wszystkie polecenia trwają poniżej 2 s.

1. `build-rel/sr_search --iterations 40` — sprężyna generowana w pamięci. Pokazać hall of fame
   i wybrany wzór o 5 węzłach, testowe MSE ≈ 0.
2. Rzut pionowy (train/validation/test z `artifacts/data`):
   ```sh
   D=artifacts/data/projectile_clean
   build-rel/sr_search ${D}_train.csv ${D}_validation.csv ${D}_test.csv --seed 2 --iterations 200 --population 100
   ```
   Wynik `*(t, -(v0, *(t, 4.905)))`, czyli `t·(v0 − 4,905 t)`. Wspomnieć, że ten sam wynik daje
   kompilacja Debug: przebieg jest deterministyczny.
3. `build-rel/sr_constraints` — komunikaty reguł oraz „Admissible … 69 of 69”.
4. `build-rel/sr_custom` — na końcu `hypot(Fy, Fx)` ze stratą Hubera 0.
5. `presentation/visualizer.html`:
   - **sekcja 1:** klawiszem `→` przejść 3–4 cykle; kroki podświetlają się po kolei. Pokazać jedno krzyżowanie
     (trzy drzewa), a potem zaznaczyć „tylko nowe rekordy”;
   - **sekcja 2:** wybrać „Rzut pionowy (ziarno 2)”, nacisnąć `spacja` (odtwarzanie ok. 9 s). Front Pareta
     schodzi w dół na złożoności 7, a punkty leżą na przekątnej;
   - wybrać „Przypadek nieudany” i pokazać, że front utknął;
   - **sekcja 3:** tabela frontu z zaznaczonym wybranym wzorem.

Plan awaryjny: jeżeli terminal zawiedzie, wystarczy sama wizualizacja (dane są zapisane),
a jeżeli zawiedzie przeglądarka, slajdy 6, 8 i 14 pokazują te same uruchomienia.

## Możliwe pytania

- **Dlaczego etapy 09.11 i 16.11 są opóźnione?** Zostały zrealizowane razem z 23.11; harmonogram
  od 30.11 jest niezagrożony, a wszystkie kryteria ukończenia tych etapów mają testy.
- **Czym to się różni od PySR?** Jedna populacja zamiast 31 wysp, przeszukiwanie współrzędnych
  zamiast BFGS, brak wyżarzania, adaptacyjnej kary złożoności i upraszczania wzorów. Ograniczenie
  wartości wykładnika jest dodatkiem względem PySR.
- **Dlaczego p = 0,7 w turnieju?** Pomiar: przy 0,9 rzut pionowy 0/3, przy 0,7 3/3 (jedna
  populacja potrzebuje mniejszej presji selekcji niż model wysp).
- **Skąd pewność, że limity są zawsze spełnione?** Generator tworzy dopuszczalne drzewa, każdy
  potomek przechodzi przez `violation()`, a test własności sprawdza 4000 kroków dla 10 ziaren
  i to, że część surowych propozycji rzeczywiście narusza limity.
- **Dlaczego nie chroniony `pow` / dzielenie?** Wzór, który działa tylko dzięki ukrytemu `|x|`
  albo `x/0 = 0`, nie jest fizycznie poprawny; taki kandydat dostaje stratę ∞, jak w PySR.
- **Czy wyniki są powtarzalne?** Tak, dla ustalonego ziarna i bez limitu czasu (test porównuje
  dwa przebiegi co do bitu; Debug i Release dają ten sam wzór).
