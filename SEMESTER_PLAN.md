# Regresja symboliczna w C++23 — plan projektu

## 1. Temat i cel projektu

**Temat:** implementacja algorytmu regresji symbolicznej w C++23 i porównanie jego jakości oraz wydajności z PySR.

Celem projektu jest opracowanie programu, który na podstawie danych tabelarycznych wyszukuje interpretowalne wzory matematyczne. Przewidziano implementację podstawowych mechanizmów wyszukiwania ewolucyjnego oraz konfiguracji wyrażeń, a następnie ocenę poprawności, jakości odtwarzania zależności i wydajności rozwiązania. Implementacja w C++ została zaakceptowana przez prowadzącego; przyjętym standardem projektu i kolejnych zadań jest **C++23**.

## 2. Pytania badawcze

1. W jakim stopniu opracowany algorytm odtwarza syntetyczne zależności fizyczne na danych bez szumu i z szumem?
2. Jak rozwiązanie wypada na tle PySR pod względem błędu testowego, złożoności wzorów i czasu działania przy jednakowych warunkach obliczeniowych?

## 3. Zakres projektu

Projekt obejmie:

- Wyszukiwanie ewolucyjne w jednej populacji: inicjalizację, selekcję, mutacje i podstawowe krzyżowanie; konfigurację rozmiaru populacji, liczby iteracji, ziarna losowości i warunków zatrzymania.
- Reprezentację wyrażeń jako drzew, nazwane zmienne, wczytywanie CSV, obliczanie wartości, predykcję oraz podstawową optymalizację stałych przez przeszukiwanie kolejnych współrzędnych.
- Wybór operatorów unarnych i binarnych, rejestrację własnych operatorów i funkcji straty, ograniczenia złożoności wyrażeń i argumentów operatorów oraz prosty szablon ze stałą strukturą zewnętrzną i jednym uczonym poddrzewem.
- Testy poprawności, pomiary wydajności, porównanie z PySR i zwięzły raport LaTeX. Algorytm, testy i narzędzie do pomiarów zostaną opracowane w C++23; Python posłuży wyłącznie do uruchamiania PySR. Własne funkcje będą udostępniane przez niewielkie API C++.

Zakres nie obejmuje wielu populacji, GPU, interfejsów do innych języków ani pełnej zgodności funkcjonalnej z PySR.

Porównanie obejmie siłę sprężystości, ruch pocisku i oscylacje harmoniczne, każdorazowo bez szumu i z szumem: **3 zależności × 2 warianty × 3 ziarna × 2 systemy = 36 uruchomień**, każde z **90-sekundowym budżetem wyszukiwania**. Oba systemy otrzymają identyczne dane i podziały, operatory, funkcję straty, limity złożoności oraz precyzję na tym samym CPU i z jednym wątkiem. Koszt startu i kompilacji JIT zostanie oddzielony od wyszukiwania. Zapisane zostaną wersje, konfiguracje i nieudane uruchomienia, a porównanie przedstawi błąd testowy, złożoność i zmierzony czas działania. Wybór modelu będzie oparty na danych walidacyjnych; przyspieszenie nie jest zakładane z góry.

## 4. Plan realizacji

Rezultaty poszczególnych etapów mają być gotowe na rozpoczęcie każdego spotkania. Na każde zajęcia przewidziano krótką prezentację lub raport o postępach. **02.11.2026 jest dniem wolnym od zajęć**, dlatego kolejny etap po 26 października przypada na 9 listopada.

| Data | Etap i planowane prace | Rezultat / kryterium ukończenia |
|---|---|---|
| **12.10.2026** | **Cel i harmonogram.** Etap obejmuje przedstawienie problemu, wymaganych funkcji i granic implementacji. Zostanie określona rola danych syntetycznych i porównania z PySR w ocenie rozwiązania. Proponowany zakres, terminy etapów i organizacja prezentacji końcowej zostaną uzgodnione z prowadzącym. | Harmonogram i zakres przedstawione do zatwierdzenia; nierozstrzygnięte kwestie zapisane. |
| **19.10.2026** | **Przygotowanie projektu i punktu odniesienia.** Etap obejmuje utworzenie minimalnego projektu C++23/CMake i próbne uruchomienie PySR w celu sprawdzenia środowiska porównania. Zostaną zdefiniowane trzy zależności fizyczne, warianty szumu oraz podział na dane treningowe, walidacyjne i testowe. Powstanie szkic raportu LaTeX obejmujący cel, krótki przegląd istniejących prac i plan oceny. | Projekt się kompiluje; PySR eksportuje wzór i błąd; specyfikacja danych jest zapisana; szkic raportu się kompiluje. |
| **26.10.2026** | **Wyrażenia i obliczanie wartości.** Wzory będą reprezentowane jako drzewa zawierające nazwane zmienne, stałe oraz operatory unarne i binarne. Implementacja obejmie wczytywanie CSV, obliczanie wartości wyrażeń i niewielkie API rejestracji operatorów oraz funkcji straty. Obliczenia wzorcowe i przypadki niepoprawnych danych pozwolą przygotować sprawdzoną podstawę wyszukiwania. | Wyrażenia wzorcowe dają oczekiwane wartości; niepoprawne dane i operacje arytmetyczne są obsłużone; przykłady API działają. |
| **09.11.2026** | **Wyszukiwanie ewolucyjne.** Zostaną zaimplementowane losowa inicjalizacja, selekcja turniejowa, mutacje i podstawowe krzyżowanie w jednej populacji. Rozmiar populacji, liczba iteracji, ziarno losowości i warunki zatrzymania będą konfigurowalne. Weryfikacja obejmie zachowanie poprawnej struktury drzew przez operacje wyszukiwania oraz zapis najlepszego wzoru wraz z wartością straty. | Wyszukiwanie z ustalonym ziarnem kończy się zgodnie z ustawionymi regułami; testy struktury przechodzą; wynikowy wzór i strata są zapisane. |
| **16.11.2026** | **Weryfikacja wersji minimalnej (MVP).** Etap obejmuje podstawową optymalizację stałych przez przeszukiwanie kolejnych współrzędnych i przechowywanie najlepszego kandydata dla każdej złożoności. Wynik wyszukiwania zostanie połączony z predykcją na nowych danych. Optymalizacja współczynników będzie testowana na ustalonym wyrażeniu liniowym niezależnie od losowego wyszukiwania, a następnie zostanie sprawdzony cały przebieg programu. | Dopasowanie i predykcja działają; optymalizacja stałych poprawia wynik ustalonego wyrażenia; powtarzalny test podstawowy z ustalonym ziarnem przechodzi. |
| **23.11.2026** | **Konfigurowalność i ograniczenia.** Zostaną zaprezentowane własne operatory unarne i binarne oraz własna funkcja straty bez zmieniania pętli wyszukiwania. Implementacja obejmie limity rozmiaru i głębokości wyrażeń oraz złożoności argumentów operatorów, w tym ograniczony wykładnik potęgi. Przypadki graniczne i wygenerowane wyrażenia pozwolą sprawdzić, czy mutacje i krzyżowanie przestrzegają tych limitów. | Każde rozszerzenie ma działający przykład; testy ograniczeń i granicznych przypadków arytmetycznych przechodzą; wyszukiwanie tworzy dopuszczalnych kandydatów. |
| **30.11.2026** | **Ukończenie wymaganych funkcji.** Zostanie dodany stały szablon z jednym uczonym poddrzewem i jawnymi listami dozwolonych zmiennych. Testy sprawdzą, czy wyszukiwanie zmienia wyłącznie dopuszczone poddrzewo i korzysta tylko z dozwolonych zmiennych. Etap zakończy się ukończeniem narzędzia do pomiarów oraz zamrożeniem danych, konfiguracji i implementacji używanej do porównania. | Wszystkie wymagane funkcje mają działające przykłady; testy przechodzą; niezmienniki szablonu są zachowane; oba systemy eksportują porównywalne wyniki. |
| **07.12.2026** | **Pomiary wydajności i porównanie.** Etap obejmuje 36 uruchomień porównawczych na danych bez szumu i z szumem oraz niewielki mikropomiar obliczania wyrażeń. Zestawienie obejmie błąd testowy, złożoność wzorów i czas działania dla różnych ziaren, z zachowaniem nieudanych uruchomień i oddzieleniem kosztu startu. Pomiary posłużą do opracowania wyników i dyskusji w raporcie, również dla przypadków nieudanego odtworzenia zależności. | Wszystkie 36 uruchomień i pomiary wydajności są zapisane; tabela porównawcza oraz szkic wyników i dyskusji są gotowe. |
| **14.12.2026** | **Przygotowanie pakietu końcowego.** Etap obejmuje rozwiązanie pozostałych problemów i ukończenie zwięzłego raportu LaTeX wraz z wnioskami i instrukcją odtworzenia wyników. Zostaną sprawdzone kompilacja w czystym katalogu, wyniki ponownych testów i przebieg pokazu działającego programu. Źródła, konfiguracje, surowe wyniki i raport będą gotowe do wspólnego oddania. | Kod, testy, pomiary wydajności, porównanie z PySR i raport są ukończone; kompilacja w czystym katalogu i pokaz działają; końcowy raport LaTeX się kompiluje. |
| **21.12.2026** | **Prezentacja końcowa i oddanie.** Prezentacja obejmie działanie programu na małym zbiorze danych oraz omówienie odtworzonego wzoru i wyników porównania. Zostaną oddane źródła, raport i zapis eksperymentów, a następnie wprowadzone końcowe poprawki, jeśli będą potrzebne. **21 grudnia jest terminem wstępnym** w ostatnim tygodniu, **21–27.12.2026**; dokładna data prezentacji wymaga potwierdzenia. | Działający program zaprezentowany; kompletny pakiet projektu oddany; końcowe uwagi uwzględnione. |

**Wszystkie rezultaty będą gotowe do 14 grudnia.** Ostatni tydzień jest przeznaczony na prezentację, oddanie projektu i poprawki.

## 5. Oczekiwane wyniki

Oczekiwane rezultaty projektu obejmują:

- Działający program C++23 wraz z kodem źródłowym, konfiguracjami, testami i przykładami wymaganych funkcji, gotowy do prezentacji na danych syntetycznych.
- Zapis eksperymentów i zestawienie błędu testowego, złożoności wzorów oraz czasu działania dla opracowanej implementacji i PySR, z uwzględnieniem nieudanych uruchomień i instrukcją odtworzenia wyników.
- Kompilujący się raport LaTeX przedstawiający istniejące prace, cel, metody i dane, wyniki, dyskusję oraz wnioski, a także prezentację działającego rozwiązania.

Wyniki mają stanowić podstawę do oceny możliwości i ograniczeń implementacji; nie zakłada się z góry przewagi nad PySR ani dokładnego odtworzenia każdej zależności.

Źródła: [Zadanie przedmiotu](https://home.agh.edu.pl/~thachaj/sp1/Studio%20projektowe%201.md), [Kalendarz AGH](https://www.agh.edu.pl/studenci/harmonogram-roku-akademickiego), [API PySR](https://pysr.ai/v2.6.0/api).
