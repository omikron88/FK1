# FK-1 Emulator

Základ multiplatformního emulátoru československého počítače FK-1 v C++20 a SDL3.

Aktuální první milník obsahuje:

- samostatnou knihovnu `fk1_core` bez závislosti na SDL,
- procesor Z80 běžící na 4 MHz přes knihovnu `redcode/Z80`,
- deterministický společný čas 12 MHz (tři tiky na takt CPU),
- přepínatelnou mapu ROM/Video RAM/RAM podle skutečného zapojení,
- tři obvody 8255 PPI s režimy 0/1, BSR a handshake signály,
- čítač 8253 se šesti režimy, BCD, LSB/MSB sekvencemi a count latch,
- UART 8251 s asynchronním i synchronním režimem a sériovými hodinami z 8253,
- obousměrný binárně transparentní raw TCP most pro sériový port,
- prioritní řadič 3214 s maskou, prioritou I7–I0 a vektory IM2,
- trvalé video/myš latche a úrovňové propojení požadavků PPI, PIT a UART,
- časovanou dvoufázovou kvadraturu kuličkové myši se dvěma tlačítky,
- ASCII klávesnici s Ctrl kódy, speciálními klávesami a emulovaným autorepeatem,
- virtuální tiskárnu Centronics s handshake 8255 a binárním výstupem do souboru,
- dvě 8palcové mechaniky s přesnou rotací 360 RPM, Track 00, Write Protect a Index,
- časované úplné FM stopy, které oddělují datový bajt a masku hodinových pulzů,
- import sektorových obrazů `.8sd` do úplné IBM 3740 stopy se skewem, gapy a CRC,
- čtení, zápis a formátování low-level FM bajtů přes handshake datové 8255,
- zpětnou analýzu změněných stop a uložení platných sektorů do `.8sd`,
- základ video RAM a převod obrazu 512 × 256 podle `hardware-spec.md`,
- bootovací ROM vloženou do programu a přepínatelnou pomocí `--rom`,
- Dear ImGui rozhraní s menu, lištou mechanik, stavovým řádkem a nativními
  dialogy SDL3,
- malé testy časování a video adresace,
- CI sestavení na Linuxu a Windows.

Obvody 8255, 8253, 8251 a prioritní řadič jsou připojené k portové mapě.
Hardwarové vedlejší účinky čtení skupin `0x30`, `0x50` a `0x70` a zápisů do
`0x30`, `0x50` a `0x70` jsou oddělené stejně jako ve skutečném dekodéru.
Bez zvoleného výstupního souboru se tiskárna hlásí offline. SDL myš se po
zachycení ukazatele převádí na původní fázové signály AX/BX a AY/BY.

## Požadavky

- CMake 3.24 nebo novější,
- kompilátor s podporou C++20,
- Git,
- SDL3 3.2 nebo novější.

CMake nejprve hledá nainstalované SDL3. Pokud je nenajde, standardně stáhne ověřenou stabilní verzi SDL 3.4.14 pomocí `FetchContent`. Automatické stažení lze vypnout volbou `-DFK1_FETCH_SDL3=OFF`.

Grafické rozhraní používá připnutou verzi
[Dear ImGui](https://github.com/ocornut/imgui), kterou CMake stáhne a staticky
připojí k frontendu. Dear ImGui je dostupné pod licencí MIT.

Procesor používá [redcode/Z80](https://github.com/redcode/Z80) a jeho
hlavičkovou závislost [redcode/Zeta](https://github.com/redcode/Zeta). CMake
standardně stáhne revize připnuté v `CMakeLists.txt` a sestaví je staticky jako
součást projektu. Stažení lze vypnout pomocí `-DFK1_FETCH_Z80=OFF`, pokud je
statický CMake balíček Z80 už nainstalovaný. Obě knihovny jsou dostupné pod
LGPL-3.0-or-later; výsledný emulátor zůstává pod GPL-3.0.

## Linux

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/fk1
```

### Linuxový build se statickými knihovnami projektu

Volba `FK1_STATIC_BUILD=ON` na Linuxu staticky připojí SDL3, Dear ImGui,
Z80/Zeta, `libstdc++` a `libgcc`. Bootovací ROM je stejně jako v každém jiném
buildu vložena přímo v programu. Glibc a systémový grafický stack zůstávají
dynamické; v cílovém systému proto musí být dostupné odpovídající runtime
knihovny X11 nebo Wayland a grafický ovladač.
Při sestavení SDL3 ze zdrojů jsou zároveň potřeba vývojové hlavičky alespoň
jednoho z těchto grafických backendů.

```sh
cmake -S . -B build/linux-static \
  -DCMAKE_BUILD_TYPE=Release \
  -DFK1_FETCH_SDL3=ON \
  -DFK1_STATIC_BUILD=ON
cmake --build build/linux-static --parallel 2
ctest --test-dir build/linux-static --output-on-failure
./build/linux-static/fk1
```

Výsledný soubor nepotřebuje `libSDL3.so`, `libstdc++.so` ani `libgcc_s.so`.
Je nativní pro architekturu, na které vznikl: build pro Raspberry Pi je proto
nutné vytvořit přímo na `aarch64`/`armhf`, build pro běžné PC na `x86_64`.
Dynamické závislosti lze zkontrolovat příkazem:

```sh
ldd build/linux-static/fk1
```

## Windows — MSYS2 UCRT64

Projekt je na Windows primárně ověřován v prostředí MSYS2 UCRT64. V terminálu
**MSYS2 UCRT64** nejprve nainstalujte nástroje a nativní SDL3:

```sh
pacman -Syu
pacman -S --needed \
  git \
  mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-sdl3
```

Potom projekt sestavte v témže UCRT64 terminálu:

```sh
cmake -S . -B build/ucrt64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DFK1_FETCH_SDL3=OFF
cmake --build build/ucrt64 --parallel
ctest --test-dir build/ucrt64 --output-on-failure
./build/ucrt64/fk1.exe
```

Volba `FK1_FETCH_SDL3=OFF` zaručí, že se použije SDL3 spravované MSYS2 a
nezačne se sestavovat další kopie knihovny přes `FetchContent`.

### Samostatný statický EXE

Volba `FK1_STATIC_BUILD=ON` vytvoří samostatný `fk1.exe`, do kterého se
staticky připojí SDL3, Dear ImGui, `libstdc++`, `libgcc` a `libwinpthread`:

```sh
cmake -S . -B build/ucrt64-static -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DFK1_FETCH_SDL3=OFF \
  -DFK1_STATIC_BUILD=ON
cmake --build build/ucrt64-static --parallel
ctest --test-dir build/ucrt64-static --output-on-failure
./build/ucrt64-static/fk1.exe
```

Výsledný program nepotřebuje vedle sebe MSYS2 ani `SDL3.dll`. Stejně jako
každý běžný Windows program nadále používá systémové knihovny Windows, například
`KERNEL32.dll` a `USER32.dll`. Plně statický Windows režim je podporován pro
MinGW/MSYS2; při použití jiného Windows toolchainu CMake skončí s jasnou chybou.

Seznam importovaných DLL lze zkontrolovat příkazem:

```sh
objdump -p build/ucrt64-static/fk1.exe | grep 'DLL Name'
```

## Bootovací a diagnostická ROM

Soubor `roms/boot.rom` se při konfiguraci CMake převede do C++ dat a vloží
přímo do knihovny `fk1_core`. Běžné ani statické `fk1.exe` proto při spuštění
nepotřebuje externí ROM soubor. Výchozí obraz má 2048 bajtů.

Alternativní bootovací nebo diagnostickou ROM lze vybrat za běhu:

```sh
./build/ucrt64/fk1.exe --rom cesta/k/diagnostic.rom
```

Přijímají se přesně obrazy o velikosti 2, 4, 8 nebo 16 KiB. V mapě po resetu
ROM zabírá rozsah `0x0000–0x3FFF`; menší obrazy se v tomto rozsahu zrcadlí
podle své skutečné velikosti. Vybraný zdroj a velikost lze zkontrolovat bez
otevření SDL okna:

```sh
./build/ucrt64/fk1.exe --rom-info
./build/ucrt64/fk1.exe --rom cesta/k/diagnostic.rom --rom-info
```

Jinou ROM lze také vložit natrvalo už při sestavení:

```sh
cmake -S . -B build/custom -G Ninja \
  -DFK1_BOOT_ROM=cesta/k/diagnostic.rom
```

## Diskový obraz `.8sd`

`disk/boot.8sd` je surový sektorový obraz v pořadí stopa–sektor. Musí mít
přesně `77 × 26 × 128 = 256 256` bajtů. Bootovací disketu připojíte do
mechaniky A takto:

V grafickém rozhraní slouží pro každou mechaniku samostatný řádek **Drive A/B**.
Tlačítko **New…** vytvoří novou zapisovatelnou image se všemi sektorovými bajty
nastavenými na `E5h`; existující soubor nepřepíše. **Open…** zobrazí nativní
dialog pro `.8sd`, **Writable** určuje, zda nově otevřený obraz dovolí zápis,
a **Eject** médium vysune. Stejné operace jsou v menu **File > Drive A/B**;
zde lze režim pouze pro čtení nebo zápis vybrat přímo. Výchozí stav je bezpečně
pouze pro čtení.

Před výměnou nebo vysunutím zapisovatelného média se low-level stopy analyzují
a uloží. Neplatný nově vybraný soubor nevysune právě používanou disketu. Pokud
nelze změněné médium převést nebo uložit, zůstane vložené v mechanice a chyba se
ukáže ve stavovém řádku.

Stejné obrazy lze nadále připojit už na příkazové řádce:

```sh
./build/ucrt64/fk1.exe --disk-a disk/boot.8sd
```

Volba `--disk-b` připojí stejný formát do mechaniky B. Tyto dvě volby zůstávají
bezpečně pouze pro čtení. Pro zápis použijte výslovnou variantu `-rw`, ideálně
nejprve na kopii obrazu:

```sh
cp disk/boot.8sd disk/work.8sd
./build/ucrt64/fk1.exe --disk-a-rw disk/work.8sd
```

K dispozici je také `--disk-b-rw`. Při připojení se každý sektorový obraz
rozvine do celé časované IBM 3740 FM stopy: 5 208 bajtů na otáčku, fyzický skew
6, IAM/IDAM/DAM, mezery a CRC-16. Řadič čte i zapisuje jednotlivé low-level
bajty podle skutečné rotační polohy; neprovádí okamžité sektorové operace.

Při normálním ukončení se zapisovatelná stopa znovu analyzuje podle IDAM a DAM.
Jestliže je přítomno všech 77 × 26 sektorů, jejich 128bajtová datová pole se
uloží zpět do stejného `.8sd`. Neúplná nebo nečitelně naformátovaná stopa se
neuloží a původní soubor zůstane beze změny.

Frontend používá pro kompatibilitu výchozí šířku Index impulzu 1 700 µs.
Lze ji změnit nebo vypnout:

```sh
./build/ucrt64/fk1.exe --disk-a disk/boot.8sd --index-us 400
./build/ucrt64/fk1.exe --disk-a disk/boot.8sd --index-us 0
```

## Tiskárna Centronics

Virtuální tiskárnu připojí volba `--printer`. Výstupní bajty se bez převodu
znakové sady nebo konců řádků přidávají do zvoleného binárního souboru:

```sh
./build/ucrt64/fk1.exe --disk-a disk/boot.8sd --printer vystup.prn
```

S touto volbou je PC5 (ONLINE/SELECT) v jedničce. Každý bajt z PA první 8255 se
zapíše do souboru a virtuální tiskárna vytvoří `/ACK_A`; při povoleném INTE tak
vznikne I0. Bez `--printer` zůstává PC5 v nule, tiskárna data nepotvrdí a žádný
I0 nevytvoří. Soubor se otevírá v režimu append, takže dřívější obsah nemaže.

## Sériový port přes TCP

Dvě běžící instance lze propojit přímo. První otevře poslouchající konec:

```sh
./build/ucrt64/fk1.exe --disk-a disk/boot.8sd --serial-listen 127.0.0.1:2025
```

Druhá se k němu připojí:

```sh
./build/ucrt64/fk1.exe --disk-a disk/boot.8sd --serial-connect 127.0.0.1:2025
```

Volby `--serial-listen` a `--serial-connect` se vzájemně vylučují. Listener
přijme vždy jednoho klienta a po jeho odpojení znovu čeká. Klient se po ztrátě
spojení pokouší připojit znovu každou sekundu. Stav spojení se projeví na
vstupech CTS a DSR 8251; při rozpojení jsou oba neaktivní. Bez kterékoli sériové
volby zůstává CTS aktivní kvůli původním diagnostickým rutinám a DSR neaktivní.

Přenos je surový osmibitový TCP stream: emulátor nepřidává echo, nemění konce
řádků a nepoužívá Telnet protokol. Lze proto připojit také terminál nastavený na
režim „Raw“, ale ne Telnet. Příchozí bajty se serializují podle právě
naprogramovaného režimu, parity a dělení 8251 a vstupují přes RxC z 8253.
Odchozí bajt se odešle do TCP až po dokončení celého emulovaného rámce na TxC.
Fronty jsou omezené a při zaplnění využijí TCP backpressure.
Při zaplnění výstupní fronty emulátor dočasně deaktivuje CTS, takže 8251
pozastaví další rámce bez neomezeného růstu paměti.

Pro propojení na jednom počítači je nejbezpečnější `127.0.0.1`. Adresa
`0.0.0.0` zpřístupní listener i z okolní sítě, takže ji používejte jen záměrně.

Okno emulátoru se ukončuje jeho zavřením nebo přes **File > Exit**. Klávesa F5
pozastaví nebo obnoví emulaci a F12 zachytí či uvolní myš. Escape se nadále
předává emulovanému stroji jako kód `0x1B`.

## Sestavení pouze jádra

Pro práci bez SDL lze frontend vypnout:

```sh
cmake -S . -B build -DFK1_BUILD_FRONTEND=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Architektura

`fk1_core` vlastní procesor, fyzickou ROM, 16 KiB Video RAM a 64 KiB hlavní
RAM. Po resetu vidí Z80 ROM na `0x0000–0x3FFF`, Video RAM na
`0x4000–0x7FFF` a hlavní RAM na `0x8000–0xFFFF`. Čtení libovolného zrcadla
I/O skupiny `0x30` zpřístupní hlavní RAM v celém 64KiB prostoru; čtení skupiny
`0x50` vrátí výchozí mapu. Horních 32 KiB je v obou mapách tatáž fyzická RAM a
videoobvod stále čte samostatnou Video RAM.

Všechny emulované události postupují po společné 12MHz časové ose. SDL3 zůstává
na hranici aplikace: předává hostitelské vstupy, zobrazuje hotový framebuffer a
pouze reguluje rychlost běhu. Tím hostitelský čas ani SDL události neurčují
pořadí emulovaných hardwarových událostí.

### Periferní obvody rodiny 8080

I/O registry zachovávají hardwarová zrcadla; skupinu vybírá `port & 0x70` a
registr 8255/8253 bity A1–A0. UART používá pouze A0.

| Skupina | Model |
|---:|---|
| `0x00` | 8255 tiskárny a klávesnice |
| `0x10` | tříkanálový 8253 |
| `0x20` | datová 8255 disku |
| `0x30` | zápis masky prioritního řadiče 3214 |
| `0x40` | 8251 UART |
| `0x50` | pomocný diskový registr |
| `0x60` | řídicí 8255 disku a videa |
| `0x70` | video I4 latch a surové signály myši/I3 |

PPI modeluje mode-set, BSR, vstupní a výstupní latche, aktivně nízké
`STB/ACK/OBF`, `IBF`, interní povolení přerušení a `INTR`. PIT podporuje režimy
0–5, binární i BCD počítání, všechny přístupové sekvence a latch příkaz; 8254
Read-Back je záměrně ignorován. Kanál 0 dostává 50Hz V impulzy a kanál 1 běží
z přesného 1MHz dělení společného času. Jeho výstup taktuje RxC i TxC 8251.

UART implementuje příkazy a stavové bity 8251, dělení ×1/×16/×64, sériové
vysílání a příjem, chyby a synchronní režim s jedním nebo dvěma SYN znaky.
To zahrnuje inicializační sekvenci `0x3C, 0x55, 0x55, 0x33` použitou v
`doc/DIAG.MAC`. Volitelný raw TCP frontend připojuje dokončené vysílané znaky
k síti a síťové bajty vrací přes časovaný sériový vstup; neobchází tak režim ani
hodiny emulovaného obvodu.

Virtuální Centronics používá PA první 8255 v handshake režimu 1. Online stav
pochází z připojeného hostitelského souboru, přijaté osmibitové hodnoty čekají
ve frontě jádra a frontend je průběžně zapisuje jako surová binární data.

### Klávesnice

SDL key-down se převádí na logický osmibitový kód. Datové vodiče klávesnice jsou
aktivní v nule, proto se celý bajt na rozhraní klávesnice–8255 invertuje a přes
`/STB_B` předá vstupnímu handshake portu B první 8255. Uvolnění klávesy neposílá
žádný kód. Hostitelský autorepeat se ignoruje. Jádro po 0,5 s držení samo opakuje
stejný invertovaný kód přes emulovaný čas frekvencí 4 znaky/s.

Běžné znaky používají ASCII. Ctrl se řídí terminálovým převodem `znak & 0x1F`,
tedy `Ctrl+@` až `Ctrl+_` dávají `0x00–0x1F`. Escape je `0x1B`, Backspace
`0x7F`, Tab `0x09` a Enter `0x0D`.

| Hostitelská klávesa | FK-1 | Kód | Se Shift |
|---|---|---:|---:|
| Delete | ROL | `0x80` | `0x81` |
| Insert | COPY | `0x82` | `0x83` |
| End | BREAK | `0x84` | `0x85` |
| ↑ / ↓ / → / ← | kurzor | `0xC1` / `0xC2` / `0xC3` / `0xC4` | stejné |
| Home | Home | `0x8D` | stejné |
| F1 / F2 / F3 | uživatelská 1–3 | `0xD0` / `0xD1` / `0xD2` | stejné |

### Myš

Kliknutí do okna zachytí hostitelský ukazatel; klávesa F12 zachycení zapíná a
vypíná. První kliknutí slouží pouze k zachycení, aby FK-1 nedostal neúplný stisk.
Potom levé a pravé tlačítko ovládají aktivně nízké vstupy T1 a T2. Ztráta focusu
ukazatel uvolní a obě tlačítka bezpečně pustí.

Relativní pohyb nevytváří souřadnice. Jádro jej převádí na původní kvadraturní
signály po jedné hraně za 1 ms emulovaného času. Při pohybu doprava vede BX
před AX: `00 → 10 → 11 → 01 → 00`. Při pohybu dolů vede AY před BY:
`00 → 01 → 11 → 10 → 00`. Opačné směry obě posloupnosti obracejí. Osy se čtou
jako AX/BX na PD0/PD1 a AY/BY na PD2/PD3. T1/T2 jsou na PD4/PD5, v klidu
v jedničce a při stisku v nule. Každá hrana při PA3=1 nastaví I3; čtení portu
`0x70` vrátí aktuální stav a I3 zruší.

### Disketové mechaniky a časovaná FM stopa

Jádro modeluje dvě nezávislé jednostranné 8palcové mechaniky se 77 stopami.
Každá vložená disketa se otáčí rychlostí 360 RPM, tedy přesně jednu otáčku za
`2 000 000` ticků společného 12MHz času. Výběr mechaniky přes PC5 datové 8255
rotační fázi nemění a obě vložená média se otáčejí současně. Prázdná mechanika
se netočí a negeneruje Index.

Řídicí 8255 promítá stav vybrané mechaniky na PC4–PC7: Track 00, Write Protect,
pevnou jedničku a Index. Krok hlavy se dokončí sestupnou hranou PC0. Polarita
PC1 je převzata přímo z `doc/DIAG.MAC`: nula pohybuje ke stopě 0 a jednička k
vyšší stopě. Systémový reset nemění vložené médium, polohu hlavy ani rotační
fázi, pouze vrátí sdílenou volbu mechaniky na B.

`FloppyDiskImage` ukládá pro každou stopu uspořádané položky `TimedFmByte`:

```cpp
struct TimedFmByte {
    uint32_t tick;   // poloha v otáčce 0 až 1 999 999
    uint8_t data;
    uint8_t clocks; // FF běžný bajt, D7/EF dílčí masky, C7 IDAM/DAM
};
```

Tím zůstávají zachované mezery, rotační poloha i chybějící FM hodinové pulzy
adresních a datových značek. Překrývající se 32µs bajty se při sestavení stopy
odmítnou. `Ibm3740SectorImage` převádí `.8sd` do této reprezentace podle
referenční implementace FK-1, včetně sektorového skewu 6 a CRC počítaného přes
značku i pole.

Šířka Indexu zůstává výslovným parametrem
`Machine::set_floppy_index_pulse_width()` v tickách; samotné jádro má bezpečný
výchozí stav nula a SDL frontend používá 1 700 µs. Čtecí
cesta rozpoznává IDAM/DAM, předává i samotnou značku přes handshake PB a
ukončuje pole pomocí counteru 2. TOUT/I6 se podle E14/8 a komentovaných rutin
v `doc/DIAG.MAC` hlídá až při `RE+RDM`, tedy při čekání na datovou značku po
nalezeném ID; hledání adresní značky může bezpečně projít přes Index.

Zápisová cesta čeká na náběžnou hranu `OUT1`, převezme předem připravený bajt z
PA, vytvoří `/ACK_A` a potom pokračuje po 32 µs až do `OUT2`. Chybějící FM
hodinové pulzy zapisuje přímo podle C3/C4 na PA0/PA1 řídicí 8255; jejich masky
jsou `FF`, `D7`, `EF` a `C7`.
`FOR` začne fyzický zápis prvním Indexem a druhý Index jej přímo ukončí; I6
zůstává vyhrazen skutečnému hardwarovému timeoutu.
Regresní test bootuje skutečný systém a příkazem `SAVE 100 BIG.COM` ověřuje
vícesektorový zápis, zachování všech IDAM/DAM, adresář CP/M i zpětný export do
`.8sd`.

### Přerušení 3214

Zápis do libovolného zrcadla skupiny `0x30` povolí 0 až 8 vstupů od nejvyšší
priority I7. Řadič drží `/INT` procesoru podle nejvýše prioritního aktivního
zdroje a při Z80 INTA dodává sudé vektory IM2 `0x00` až `0x0E`. Potvrzení
požadavek samo nemaže.

I3 od myši a I4 od 50Hz videa jsou latche. Čtení `0x70` vrátí aktuální surový
bajt myši a zruší pouze I3; zápis `0x70` zruší pouze I4. PA3 a PA2 řídicí 8255
povolují zachycení nových hran/impulzů, ale již nastavený latch neruší. PD0–PD5
nesou v pořadí AX, BX, AY, BY, T1 a T2 podle listu 5 schématu.

I7 je úrovňový součet `INTR_A OR INTR_B` datové 8255 a jeho náběžná hrana
taktuje counter 2; synchronizační značka se přenese do 8255 ještě před začátkem
počítání bajtů pole. I5 sleduje `OUT2`, I2 je `TXRDY OR RXRDY`, I1 je klávesnice
`INTR_B` a I0 tiskárna `INTR_A`. I6 timeout se ruší přeprogramováním counteru 1,
vypnutím diskového časování nebo vynulováním pomocného registru, jak používá
obsluha v `boot.rom`.
