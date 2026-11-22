# Jak czytać wizualizację wyszukiwania

Ten dokument wyjaśnia prostym językiem, co pokazuje `presentation/visualizer.html`
i jak działa algorytm, który ta strona odtwarza. Wszystkie liczby pochodzą
z prawdziwego uruchomienia **„Rzut pionowy, ziarno 2”** (drugie na liście uruchomień).
Najlepiej czytać ten tekst z otwartą wizualizacją obok.

Jak otworzyć: `google-chrome presentation/visualizer.html` albo dwuklik na pliku.
Nie trzeba internetu ani kompilacji.

Ogólne wyjaśnienie całego projektu od zera (problem, drzewa, ewolucja, ograniczenia,
testy, pytania prowadzącego) jest w
[WYJASNIENIE_DLA_POCZATKUJACYCH.md](../WYJASNIENIE_DLA_POCZATKUJACYCH.md). Ten dokument
skupia się na wizualizacji i na tym, co dokładnie dzieje się w jednym przebiegu.

---

## 1. O co w ogóle chodzi

Mamy tabelę pomiarów. W rzucie pionowym są to trzy kolumny (przykładowe wiersze):

| t (czas, s) | v0 (prędkość początkowa, m/s) | h (wysokość, m) |
|---|---|---|
| 0,50 | 12 | 4,77 |
| 1,00 | 15 | 10,10 |
| 2,00 | 20 | 20,38 |
| … (256 wierszy) | | |

Chcemy znaleźć **wzór**, który z `t` i `v0` liczy `h`. Prawdziwy wzór fizyczny to

> h = v0·t − 4,905·t²

ale **algorytm go nie zna**: dostaje tylko tabelę i ma sam wymyślić wzór. To jest
*regresja symboliczna*. W odróżnieniu od sieci neuronowej wynikiem jest czytelny
wzór, a nie „czarna skrzynka”.

---

## 2. Wzór jako drzewo

Algorytm zapisuje każdy wzór jako **drzewo**. Wzór `t·(v0 − 4,905·t)` (to samo co
`v0·t − 4,905·t²`, tylko inaczej zapisane) wygląda tak:

```
          ×
        /   \
       t     −
           /   \
         v0     ×
              /   \
          4,905    t
```

Jak czytać drzewo: **od dołu do góry**.
- **Liście** (na dole) to zmienne z tabeli (`t`, `v0`) albo liczby (`4,905`).
- Każdy **węzeł wyżej** to działanie (`+`, `−`, `×`, `÷`, `cos`…) wykonywane na tym,
  co jest pod nim.
- Tu: najpierw `4,905 × t`, potem `v0 − (wynik)`, na końcu `t × (wynik)`.

**Złożoność** wzoru to liczba węzłów; ten ma 7. Prostszy wzór łatwiej zrozumieć,
dlatego algorytm szuka wzoru, który jest jednocześnie **dokładny** i **prosty**.

W wizualizacji kolory węzłów oznaczają:
- szary owal: działanie (operator);
- niebieski prostokąt: zmienna (kolumna danych);
- jasnopomarańczowy prostokąt: liczba (stała).

---

## 3. Jak działa algorytm, w jednym obrazku

Algorytm działa jak **hodowla wzorów** (algorytm ewolucyjny):

1. Na start losuje **100 przypadkowych wzorów**. To jest **populacja**; większość jest
   bardzo słaba.
2. Potem bez przerwy powtarza **cykl**, który tworzy jeden nowy wzór:

```
  wybierz rodzica ──► zmień go ──► sprawdź limity ──► policz błąd ──► wstaw do populacji
        ▲                                                                   │
        └───────────────────────────────────────────────────────────────────┘
```

3. **100 cykli = 1 iteracja.** W tym uruchomieniu było 200 iteracji, czyli
   **20 000 cykli**, w 1,7 sekundy.

Dlaczego to działa? Dobre wzory częściej wygrywają wybór rodzica, więc częściej mają
„dzieci”. Dzieci są lekko zmienionymi kopiami rodziców, więc część z nich jest
jeszcze lepsza. Po tysiącach cykli w populacji zostają coraz lepsze wzory.

**Błąd** (MSE) mówi, jak bardzo wzór myli się na danych: dla każdego wiersza liczymy
różnicę między wartością ze wzoru a pomiarem, podnosimy do kwadratu i uśredniamy.
**0 oznacza idealny wzór**, a większa liczba gorszy.

---

## 4. Strona krok po kroku

### 4.1 Góra strony

- **„Co pokazuje ta strona?”**: skrót powyższego cyklu.
- **„To uruchomienie”**: jakie dane, jaki jest prawdziwy wzór (dla porównania), jakie
  działania wolno było używać (`+ − × ÷`), ile wzorów w populacji i jaki
  **znaleziony** wzór.

Na liście w prawym górnym rogu wybierasz uruchomienie (opis wszystkich w rozdziale 5).

### 4.2 Sekcja 1: jeden cykl, krok po kroku

Tu widać, **jak powstaje jeden nowy wzór**. Przycisk **Krok ▶** (albo klawisz `→`)
przechodzi przez 6 etapów. Pod paskiem etapów jest ramka z opisem tego, co się dzieje.

| Etap | Co się dzieje | Co widać na ekranie |
|---|---|---|
| 1. Turniej | Z populacji losujemy 8 wzorów. Zwykle (w 70% przypadków) wygrywa najlepszy z nich, czasem słabszy. Zwycięzca to **rodzic**. | drzewo rodzica i jego błąd |
| 2. Zmiana | Rodzic jest kopiowany i kopia zostaje zmieniona: **mutacja** (80%) albo **krzyżowanie** (20%). | pojawia się drzewo **potomka**; to, co się zmieniło, jest **obramowane na pomarańczowo** |
| 3. Limity | Sprawdzamy, czy nowy wzór nie jest za duży (tu max 20 węzłów i głębokość 10). Jeśli jest, zmiana jest losowana od nowa. | ile propozycji odrzucono |
| 4. Ocena | Liczymy błąd potomka na danych. | błąd potomka obok błędu rodzica |
| 5. Stałe | U 5% potomków liczby we wzorze są „dostrajane”, żeby błąd był jak najmniejszy. | czy dostrajanie zaszło |
| 6. Zastąpienie | Potomek zajmuje w populacji miejsce **najstarszego** wzoru. | pasek populacji: pomarańczowy kwadrat to miejsce potomka |

Przyciski:
- **Następny cykl ▶▶** (`Shift`+`→`) i **◀ Poprzedni cykl** (`←`) przełączają cykle;
- **Auto** samo przechodzi przez kolejne kroki i cykle;
- zaznaczenie **„tylko cykle, które pobiły rekord”** pokazuje wyłącznie cykle, w których
  powstał najlepszy dotąd wzór.

Zapisane są pierwsze 60 cykli (żeby pokazać mechanikę) i wszystkie cykle z rekordem.

#### Przykłady z tego uruchomienia

Każdy przykład można otworzyć bezpośrednio linkiem, dopisując podaną końcówkę
do adresu `visualizer.html`.

**Wstawienie węzła** (cykl 6, `#run=1&event=5&stage=1`).
Rodzic `1,944 − t·(−0,456·0,172)` ma błąd 78,9. Mutacja wstawiła nad liczbą `0,172`
nowe działanie `+` z nową liczbą `−0,409`:

```
 rodzic:   ... × (−0,456,  0,172 )
 potomek:  ... × (−0,456,  (−0,409 + 0,172) )
```

Nowe węzły mają grubą pomarańczową ramkę. Przerywana ramka przy `0,172` znaczy
„tu nastąpiła zmiana, ale ten fragment został”. Błąd potomka to 82,5, czyli gorzej.

**Zmiana operatora** (cykl 9, `#run=1&event=8&stage=1`).
`1,343 · 0,409·v0` zmienia się w `1,343 + 0,409·v0`: znak `×` zamienił się na `+`,
a argumenty zostały te same. Błąd wzrósł z 17,7 do 20,8.

**Usunięcie węzła** (cykl 7, `#run=1&event=6&stage=1`).
`1,056 + 0,353/t` traci działanie `+` razem z liczbą `1,056`; zostaje `0,353/t`.
Drzewo maleje, a błąd rośnie ze 100,4 do 117,5.

**Krzyżowanie** (cykl 16, `#run=1&event=15&stage=1`).
Pierwszy rodzic `v0 / (1,566·v0 / (v0 − 1,019))` (błąd 16,7) i drugi rodzic
`v0 − 1,667`. Fragment `1,566·v0` z pierwszego rodzica zostaje zastąpiony fragmentem
`v0` z drugiego; oba są obramowane. Potomek to `v0 / (v0 / (v0 − 1,019))`, błąd 45,5.

#### Dlaczego gorsze dzieci też trafiają do populacji?

We wszystkich czterech przykładach potomek jest **gorszy** od rodzica i to jest
normalne. Większość zmian psuje wzór, ale:
- słaby wzór i tak rzadko wygra turniej, więc szybko zniknie, bo po 100 cyklach jego
  miejsce zajmie nowy wzór;
- czasem gorszy wzór jest krokiem pośrednim do dużo lepszego. Gdyby algorytm
  przyjmował tylko poprawy, łatwo utknąłby w miejscu.

#### Pasek populacji (krok 6)

Pasek ma tyle kwadratów, ile wzorów jest w populacji (tu 100). Potomek zawsze
zastępuje **najstarszy** wzór, czyli miejsca są zastępowane po kolei: 1, 2, 3, …,
100, 1, 2… Każdy wzór żyje więc dokładnie 100 cykli; dalej „żyją” tylko jego
potomkowie. Dzięki temu populacja się nie zatyka jednym wzorem i pozostaje
różnorodna. Szare kwadraty za pomarańczowym to kolejne miejsca do zastąpienia.

### 4.3 Sekcja 2: cały przebieg

Sekcja 1 pokazuje pojedyncze cykle, a sekcja 2 **efekt 20 000 cykli**. Po każdej
iteracji (100 cyklach) zapisano stan algorytmu. Suwak wybiera iterację, a przycisk
**Odtwórz** (lub `spacja`) odtwarza cały przebieg w około 9 sekund.

#### Historia tego uruchomienia

| Iteracja | Najlepszy wzór w tej chwili | Błąd |
|---|---|---|
| 0 (start) | `1,343 · 0,409 · v0` (losowy) | 17,7 |
| 1 | `1,358 · t · 0,409 · v0` | 6,61 |
| 5 | długi wzór z 13 węzłami | 0,81 |
| 7 | pojawia się `t · 0,615 · (v0 − t)`: **prawie właściwy kształt** | 5,06 |
| 10 | `t · (v0 − 4,905·t)`: **dokładny wzór** | 1,5·10⁻³⁰ |
| 10–200 | bez zmian: lepszego wzoru już nie ma | 1,5·10⁻³⁰ |

Co tu widać:
- Najpierw algorytm znajduje coraz dłuższe, „łatane” wzory, które coraz lepiej
  przybliżają dane (iteracje 1–9).
- W iteracji 7 pojawia się prosty wzór o dobrym kształcie, ale ze złą liczbą.
- Między iteracjami 9 i 10 powstaje drzewo `t · (v0 − c·t)`, a dostrajanie stałych
  ustawia `c = 4,905`. **Ewolucja znajduje kształt wzoru, a optymalizacja dokładne liczby.**

#### Wykres 1: „Najmniejszy błąd do tej pory”

- Oś pozioma: ile potomków (cykli) już powstało.
- Oś pionowa: najmniejszy błąd do tej pory, w **skali logarytmicznej**. Każda kolejna
  podziałka jest wielokrotnie mniejsza: 10⁰ = 1, 10⁻⁶ = 0,000001, i tak dalej.
- Linia może tylko spadać. Każdy **pionowy uskok** to znaleziony lepszy wzór.
- **10⁻³⁰** to zero z dokładnością do błędów zaokrągleń komputera: wzór jest dokładny.
  Duży uskok tuż przed 1 000 potomków to moment z iteracji 10.

#### Wykres 2: „Populacja i front Pareta”

Każdy wzór to punkt: **poziomo** liczba węzłów (złożoność), **pionowo** błąd.
- **Szare punkty** to wzory obecnie w populacji (100 punktów).
- **Niebieskie punkty połączone schodkami** to **front Pareta**: najlepszy znany wzór
  dla każdej złożoności. Na przykład „najlepszy wzór z 1 węzłem”, „najlepszy z 3”…
- **Ideał to lewy dolny róg**: wzór prosty i dokładny.

Na końcu przebiegu front ma tylko 4 wzory:

| Węzły | Wzór | Błąd |
|---|---|---|
| 1 | `9,61` (sama liczba) | 21,6 |
| 3 | `t · 8,74` | 12,8 |
| 5 | `t · (v0 − 7,28)` | 4,87 |
| 7 | `t · (v0 − 4,905·t)` | 1,5·10⁻³⁰ |

Dłuższych wzorów na froncie nie ma: wzór z 7 węzłami jest dokładny, więc nic
bardziej złożonego nie może być od niego lepsze.

#### Wykres 3: „Przewidywania a dane”

Są dwie zmienne (`t` i `v0`), więc nie da się narysować zwykłej krzywej. Każdy punkt to
jeden wiersz danych:
- **poziomo**: zmierzone `h`;
- **pionowo**: `h` policzone ze wzoru.

Jeśli wzór jest idealny, wszystkie punkty leżą na **przekątnej**. Przesuń suwak na
początek, a punkty będą rozrzucone; na koniec leżą idealnie na linii.

Dla danych z jedną zmienną (sprężyna, oscylator) wykres pokazuje szare punkty danych
i niebieską krzywą wzoru.

#### Tabelki pod wykresami

- **„Najdokładniejszy wzór w tej chwili”**: wzór o najmniejszym błędzie na danym etapie.
- **„Front Pareta w tej chwili”**: to samo co niebieskie punkty, ale w postaci wzorów.

### 4.4 Sekcja 3: wynik

Dane są podzielone na **trzy osobne zbiory**:

| Zbiór | Do czego służy |
|---|---|
| **treningowy** (256 wierszy) | na nim liczony jest błąd w trakcie wyszukiwania |
| **walidacyjny** (128 wierszy) | wybór jednego wzoru z frontu Pareta |
| **testowy** (256 wierszy) | końcowa, uczciwa ocena: algorytm nie widział tych danych |

Reguła wyboru: z frontu bierzemy **najprostszy wzór, którego błąd walidacyjny jest
co najwyżej o 5% większy od najmniejszego**. Tu wybrany został `t · (v0 − 4,905·t)`
(oznaczony „wybrany”), z błędem testowym 1,6·10⁻³⁰, czyli dokładnie.

Kafelki ze statystykami:
- **Utworzeni potomkowie:** 20 000 cykli.
- **Czas wyszukiwania:** 1,71 s.
- **Propozycje odrzucone przez limity:** 3 641. Tyle razy zmiana dawała wzór za duży
  albo za głęboki i była losowana od nowa. Dowodzi to, że limity naprawdę działają.
- **Wzory niezdefiniowane:** 36. Na przykład dzielenie przez zero; taki wzór dostaje
  błąd „nieskończony” i odpada.

---

## 5. Pięć uruchomień: na co patrzeć

| Uruchomienie | Prawdziwy wzór | Znaleziony wzór | Na co zwrócić uwagę |
|---|---|---|---|
| **Sprężyna** | F = 2,5·x + 0,75 | `(x + 0,3) / 0,4` | To jest **ten sam** wzór: (x + 0,3)/0,4 = 2,5x + 0,75. Znaleziony już w 3. iteracji. Wykres 3 pokazuje zwykłą prostą przez punkty. |
| **Rzut pionowy, ziarno 2** | h = v0·t − 4,905·t² | `t·(v0 − 4,905·t)` | Przykład opisany wyżej. |
| **Rzut z szumem i `pow`** | h = v0·t − 4,905·t² | `t·(v0 − 4,9047·t)` | Dane mają 1% szumu, więc wzór nie może być idealny: błąd testowy 0,00195 to mniej więcej poziom szumu. Wolno było używać potęgi z wykładnikiem całkowitym \|e\| ≤ 2; limity odrzuciły 3 769 propozycji. |
| **Oscylator** | x = 1,5·cos(2t + 0,3) | `cos(t + (t − cos(−1,875)))·1,5` | cos(−1,875) ≈ −0,3, więc t + (t + 0,3) = 2t + 0,3: **ten sam wzór**, tylko algorytm nie umie go uprościć. Wykres 3 pokazuje sinusoidę idealnie na punktach. |
| **Przypadek nieudany** | h = v0·t − 4,905·t² | długi wzór z 17 węzłami | Populacja **utknęła**: wykres 1 przestaje spadać, a front nie dochodzi do dokładnego wzoru. Błąd testowy 0,29. Przyczyna: tylko jedna populacja; PySR używa wielu niezależnych populacji. Dobry materiał do dyskusji o ograniczeniach. |

---

## 6. Słowniczek

| Pojęcie | Znaczenie |
|---|---|
| **Populacja** | Zbiór wzorów (tu 100), nad którymi pracuje algorytm. Ma zawsze ten sam rozmiar. |
| **Cykl** | Utworzenie jednego nowego wzoru (6 etapów z sekcji 1). |
| **Iteracja** | Tyle cykli, ile wzorów w populacji (tu 100). Po każdej iteracji dostrajane są liczby we wzorach z frontu Pareta. |
| **Turniej** | Losujemy 8 wzorów; zwykle wygrywa najlepszy i zostaje rodzicem. |
| **Rodzic / potomek** | Wzór wybrany do zmiany / nowy wzór powstały przez zmianę. |
| **Mutacja** | Mała losowa zmiana jednego drzewa: inna liczba, inne działanie, nowy lub usunięty węzeł, nowy fragment. |
| **Krzyżowanie** | Fragment jednego drzewa zastąpiony fragmentem drugiego. |
| **Limity (ograniczenia)** | Maksymalna liczba węzłów, głębokość drzewa, rozmiar argumentów działań, wykładnik potęgi. |
| **Błąd (MSE)** | Średni kwadrat różnicy między wzorem a danymi; 0 = idealnie. |
| **Złożoność** | Liczba węzłów drzewa. |
| **Front Pareta (hall of fame)** | Najlepszy znaleziony wzór dla każdej złożoności. Z niego wybierany jest wynik. |
| **Dostrajanie stałych** | Przesuwanie liczb we wzorze (przy niezmienionym kształcie), dopóki błąd maleje. |
| **Skala logarytmiczna** | Oś, na której każda podziałka to wielokrotność poprzedniej (1, 10⁻⁶, 10⁻¹²…). Pozwala pokazać na jednym wykresie i błąd 100, i 10⁻³⁰. |

---

## 7. Częste pytania

**Skąd algorytm wie, że wzór jest dobry, skoro nie zna prawdziwego wzoru?**
Porównuje wartości ze wzoru z pomiarami w tabeli (błąd MSE). Prawdziwy wzór jest
pokazany na stronie tylko po to, żebyśmy *my* mogli ocenić wynik.

**Czy to losowe? Czy za każdym razem wyjdzie co innego?**
Losowe, ale powtarzalne: przy tym samym ziarnie (*seed*) wynik jest identyczny co do
bitu. Inne ziarno daje inny przebieg; dlatego istnieje „Przypadek nieudany” z ziarnem 0.

**Co się dzieje, gdy wzór dzieli przez zero?**
Taki wzór dostaje błąd „nieskończony”, więc nigdy nie wygra turnieju i nie trafi
do frontu Pareta. Algorytm się nie zatrzymuje.

**Po co front Pareta, skoro można wziąć wzór z najmniejszym błędem?**
Bo najdokładniejszy wzór bywa niepotrzebnie skomplikowany (dopasowuje się do szumu).
Front pozwala wybrać kompromis: najprostszy wzór, który jest prawie tak dobry jak
najlepszy.

**Dlaczego wzory na ekranie wyglądają dziwnie, np. `cos(t + (t − cos(−1,875)))`?**
Algorytm nie upraszcza algebraicznie. Wzór jest poprawny, tylko zapisany „nieładnie”.
Upraszczanie to jedno z możliwych ulepszeń.

**Czy wizualizacja coś liczy na nowo?**
Nie. Odtwarza zapis uruchomienia (`traces.js`) przygotowany przez program C++
(`sr_search --trace`). Rysuje tylko drzewa i wykresy; jedynie krzywą na wykresie 3
przelicza ze wzoru, żeby ją narysować.

---

## 8. Jak to pokazać w 2–3 minuty

1. Wybierz „Rzut pionowy … ziarno 2”. Pokaż ramkę „To uruchomienie”: prawdziwy wzór
   i znaleziony wzór.
2. **Sekcja 1:** otwórz krzyżowanie (`visualizer.html#run=1&event=15&stage=0`)
   i 5 razy naciśnij `→`, za każdym razem czytając jedno zdanie z ramki opisu.
   Pokaż pomarańczowe ramki w obu rodzicach i potomku.
3. **Sekcja 2:** przesuń suwak na początek i naciśnij `spacja`. Powiedz: „błąd spada
   skokami; około iteracji 10 front Pareta spada na złożoności 7, punkty wskakują
   na przekątną: to jest dokładny wzór”.
4. **Sekcja 3:** „wybrany wzór sprawdzony na danych, których algorytm nie widział;
   3 641 propozycji odrzuciły limity”.
5. Na koniec wybierz „Przypadek nieudany”: „nie zawsze się udaje; przy jednej
   populacji algorytm może utknąć”.
