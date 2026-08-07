.Z80;
	TITLE	DIAG      program na diagnostiku mikropocitace FK-1
	ASEG;
;
EPROM	EQU	0;
CPM	EQU	1;
;
; Navod k pouziti: Pokud budeme program ladit pod CP/M, pak nasledujici instrukce
; bude OBJECT EQU CPM, provadime-li preklad pro umisteni do EPROM, bude nasledujici
; instukce OBJECT EQU EPROM.
;
OBJECT	EQU	CPM;		pod cim budeme pracovat
;
;
	COND	OBJECT EQ CPM;
	ORG	X'0100';	ukladaci adresa programu
	ELSE;
	ORG	X'0000';	umisteni v EPROM
	ENDC;
;
; Program na diagnostiku mikropocitace FK-1 urceny k zapisu do EPROM. Pomoci tohoto
; testu lze jednoduse ozivovat mikropocitac a uspesne opravovat, pokud zavada znemozni
; nasati CP/M. Nejprve je treba zkontrolovat, jestli je na D13 noha 3 a 11 jednicka.
; Neni-li, nutno opravit zavadu. Pokud je to v poradku, neni mit treba obavy o diskety,
; cely test je neznici. Tento program je genialni v tom, ze prozkusuje pocitac po
; kouskach. Vrele nedoporucuji bezhlave spousteni sequenci, ale jen po tom, kdyz vse
; spolehlive chodi, neb dalsi sequence mohou vyuzivat jiz otestovanou elektroniku.
; Jako pomucky staci televize, tristavova sonda, kousek dratu na propojky a uzemnovani,
; nebetycna trpelivost, trocha stesti a vidina tucne odmeny. Mnoho uspechu preje autor
; testu, ktery si sam vyzkousel hledani zavad.
;
; Autor:    Richard Cestr, Husova 13, 250 91 Zelenec,
; telefon:  78 81 457.
;
; 1. verse, dne 13. 8. 1989.
;
	COND	OBJECT EQ CPM;
ROP	EQU	X'8000';	ukladaci adresa pod CP/M
	LD	HL,BEGIN;	pokud jedeme pod CP/M musime se presunout abuchom
	LD	DE,ROP;		si nepodrizli vetev na ktere sedime
	LD	BC,ENDPRG-START;
	LDIR;			pokud jedeme pod CPM, presun programu do bezpeci
	JP	START;		a skok tam
BEGIN	EQU	$;
.PHASE	ROP;
	ENDC;
;
START:	DI;			hned preruseni zatrhnout
	IM	2;		nastolit mod 2 IP (do foroty)
	IN	A,(X'50');	prepnout na EPROM a videoRAM - pro sychr
;
; Test jestli vubec neco jede. Stridave se objevuji a mizi pulsy IORQ, pozorujeme sondou
; na D66/20. Pulsy se objevi 2x.
;
	LD	H,2;		2x opakovat pulsovani
TIORQ:	LD	C,0;		s jakym portem pracujeme
	LD	IY,WIORQ;	kam se pak vratit
	LD	IX,INPUT4;	kam pujde po smycce
	LD	D,1;		aby to chvilku trvalo
;
; Podprogram na chvilkove provedeni IN nebo pockani. Y := spojka, X := ad. odskoku.
; Nici: A, B, D, E.
;
INPUT1:	LD	E,255;
INPUT2:	LD	B,255;
INPUT3:	JP	(IX);		odskok na vykonnou instrukci
INPUT4:	IN	A,(C);
WAIT2:	DJNZ	INPUT3;
	DEC	E;
	JR	NZ,INPUT2;
	DEC	D;
	JR	NZ,INPUT1;
	JP	(IY);		navrat neboli 	RETURN
;
WIORQ:	LD	IY,TIORQD;	kam se vratit
	LD	D,1;		jak dlouho se bude cekat - cca momenticek
;
; Podprogram na cekani.  D := doba cekani, Y := spojka
; Nici: B, D, E, X.
;
WAIT:	LD	IX,WAIT2;
	JR	INPUT1;
;
TIORQD:	DEC	H;
	JR	NZ,TIORQ;	opakovat 2x pulsovani na IORQ
;
; Test videoram. Zapisuji se hodnoty 55, FE, AA, 01 neustale do videoram, a cyklicky se
; opakuji. Po prvnim pruchodu ma pamet obsah 55 FE AA 01 55 FE ...., pri druhem  FE AA
; 01 55 FE ... a tak dale. Je to tak zvany "sachovnicovy test" ktery je nejucinnejsi pro
; kontroly jakychkoliv druhu pameti. Chyby jsou znazorneny pomoci nudli, ktere se objevi
; na CRT. Plna nudle je jedna, prazdna je nula. Prva osmice je co se z pameti precetlo,
; druha co se melo precist. Pod tim je dvoubytova adresa. Po chybe se test automaticky
; opakuje, az kdyz je zavada odstranena, pokracuje se dal.
;
BGCRT	EQU	X'4000';	zacatek videoram
ENDCRT	EQU	X'7FFF';	konce videoRAM
;
TCRT:	XOR	A;		nulu do ROLL registru
	OUT	(X'61'),A;	nastaveni ROLL registru
	LD	IY,TCRTP;	parametry pro podprogram
;
; Test pameti. (IY) := vyssi bajt ad. zacatku pameti, (IY+1) := vyssi bajt ad. konce,
; (IY+2) := spojka, (IY+4) := ad. odskoku pred zapisem/ctenim  (zpet pres IX ci TMEM2).
; Po provedeni: Z=1 --> test prosel OK, Z=0 --> test prosel blbe. Pokud je Z=0,
; pak: A:=chybne precteny bajt, DE:= jeho adresa, X:= adresa spravneho bajtu.
; Nici: A, C, DE, HL, X.
;
R1	EQU	$+1;
TMEM:	LD	DE,TBMEM;	ad. co se bude zapisovat
TMEM1:	LD	C,0;		ze je zapis
TMEM11:	LD	H,(IY+5);
	LD	L,(IY+4);
	LD	IX,TMEM2;
	LD	A,C;		kod jestli se cte/zapisuje
	JP	(HL);		odskok pred zapisem/ctenim
TMEM2:	LD	H,(IY+0);
	LD	L,0;		ad. zacatku pameti
	LD	IX,0;
	ADD	IX,DE;		kde zacinaji kody testu
TMEM4:	LD	A,(IX);
	CP	255;
	JR	NZ,TMEM5;	neni konec seznamu kodu
R2	EQU	$+2;
	LD	IX,TBMEM;	je, delat od zacatku
	JR	TMEM4;		aby se znova nabil A
TMEM5:	BIT	0,C;
	JR	NZ,TMEMK;	dela se kontrola obsahu
	LD	(HL),A;
TMEM6:	INC	IX;		dalsi kod
	LD	A,H;
	CP	(IY+1);
	JR	Z,TMEM8;	muze byt konec
TMEM7:	INC	HL;
	JR	TMEM4;
TMEM8:	LD	A,L;
	CP	255;
	JR	NZ,TMEM7;	neni konec testovaneho cancouru
	BIT	0,C;
	JR	NZ,TMEMK3;	byla kontrola obsahu
	SET	0,C;		bude kontrola obsahu
	JR	TMEM11;
;
TMEMK:	LD	A,(HL);
	CP	(IX+0);
	JR	Z,TMEM6;	je to dobre
	EX	DE,HL;		do DE dat ad. kde byla chyba
TMEMK2:	LD	H,(IY+3);
	LD	L,(IY+2);
	JP	(HL);		navrat
;
TMEMK3:	INC	DE;
	LD	A,(DE);
	CP	255;
	JR	NZ,TMEM1;	neni vsechno hotovo
	JR	TMEMK2;
;
;
TCRT2:	JP	Z,POCRT;	test prosel kupodivu OK
	LD	IY,ERCRT1;	spojka
TRAMH8:	EXX;			uschova hlavne DE s ad.pameti
;
; Podprogram mazani videoram. Spojka je Y. Nici: BC, DE, HL.
;
RAZCRT:	LD	HL,BGCRT;
	LD	(HL),0;
	LD	DE,BGCRT+1;
	LD	BC,ENDCRT-BGCRT;
	LDIR;
	JP	(IY);
;
;
ERCRT1:	LD	HL,ERCRT2;
;
; Zobrazeni chyby v pameti. Zadni DE:=ad vadneho mista, A:=vadny bajt, X:=ad.ok bajtu,
; predni HL:=spojka.
; Nici: A, zadni AF, zadni BC, zadni HL, Y.
;
INDIK:	EXX;
	LD	HL,BGCRT;	kam psat nudle
	LD	IY,INDIK2;
	JR	NUDLE;		vukreslit nudlema vadny bajt
INDIK2:	LD	A,(IX);		spravny obsah
	LD	IY,INDIK3;
	LD	HL,BGCRT+9*(3*256)+6*256;
	JR	NUDLE;		spravny obsah
INDIK3:	LD	HL,BGCRT+120;
	LD	A,D;
	LD	IY,INDIK4;
	JR	NUDLE;		vyssi rady adresy
INDIK4:	LD	HL,BGCRT+120+9*(3*256)+6*256;
	LD	A,E;
	LD	IY,INDIK5;
	JR	NUDLE;		nizsi rady adresy zanudlit
INDIK5:	EXX;
	JP	(HL);
;
; Podprogram na zobrazeni jednoho bajtu nudlema. Y:=spojka, A:=zobrazovany bajt,
; HL:=ad. CRT. Nesmi se smazat X a DE.
; Nici: B, C, HL, zadni AF.
;
CNUDLE:	LD	IY,CNUDL1;	pro volani z mist kdy uz RAM chodi
	LD	HL,BGCRT+18*256+100;
;
NUDLE:	LD	B,8;		kolikrat delat (pocet bitu v bajtu)
NUDLE1:	LD	C,81;		vyska nudle
NUDLE5:	LD	(HL),255;
	INC	H;
	LD	(HL),255;
	JR	NUDLE3;		dokreslen svrsek, spodek, svybarveni nudle
NUDLE2:	BIT	7,A;
	JR	NZ,NUDLE5;	bude jednickova, vybarvena nudle
	LD	(HL),X'80';
	INC	H;
	LD	(HL),X'01';	nulova, tedy nevycmarana nudle
NUDLE3:	DEC	H;
	INC	HL;
	DEC	C;
	JR	NZ,NUDLE2;	neni jeste cela delka nudle
	LD	(HL),255;
	INC	H;
	LD	(HL),255;	dno nudle neboli prdelka
	EX	AF,AF';		uschova A
	LD	A,L;
	SUB	81;
	LD	L,A;		HL := puvodni adresa CRT + 256
	INC	H;
	INC	H;
	LD	A,5;
	CP	B;
	JR	NZ,NUDLE4;	neni ctvrta nudle hotova tudiz ne mezera mezi nima
	INC	H;		je, bude
NUDLE4:	EX	AF,AF';		obnova A
	RLCA;			delat dalsi bit
	DJNZ	NUDLE1;		nejsou vsechny bity hotovy
	JP	(IY);		jsou, vypadne ven
;
;
ERCRT2:	LD	IY,TCRT
TRAMH7:	LD	D,20;		cekat 10 sekund
	JP	WAIT;
;
;
; Test horni pulky RAM. Testuje se shodnou sachovnici jako videoram. Chyby jsou take
; zobrazeny stejnymi nudlemi. Prubeh testu je signalizovan v horni casti obrazovky
; ctverecky. Prazdny znamena ze se rozebehl cykl zapisu do pameti, jeho vymalovani
; ukazuje, ze se z pameti kontrolne cte. Pocet ctverecku urcuje pocet rund. Po chybe
; se test automaticky opakuje.
;
	COND	OBJECT EQ CPM;
BGRAMH	EQU	X'9000';	pokud jedeme pod CP/M tak abychomse neodmazali
	ELSE;
BGRAMH	EQU	X'8000';	jsme-li v EPROM, testuje se 2. pulka
	ENDC;
ERAMH	EQU	X'FFFF';	konec horni pulky RAM
;
POCRT:	LD	IY,TRAMH;
	JP	RAZCRT;		smazat CRT
;
TRAMH:	LD	DE,BGCRT+30;	kde budou ctverecky
	EXX;			do zadnich registru
	LD	IY,PRAMH;
	JP	TMEM;
;
TRAMH2:	EXX;
	LD	C,A;		jestli se bude delat plny nebo prazdny ctverecek
CRAMH0:	LD	HL,CRAMH3;	spojka
R3	EQU	CRAMH0+1;
CRAMH1:	LD	A,255;
	LD	(DE),A;
	INC	D;
	LD	(DE),A;
	INC	D;
	LD	(DE),A;
CRAMH2:	INC	D;
	LD	(DE),A;		svrsek ctverecku nebo jeho dno nebo vybarveni
	DEC	D;
	DEC	D;
	DEC	D;
	INC	DE;
	JP	(HL);		navrat
CRAMH3:	LD	B,25;		delka ctverecku
R4	EQU	$+1
CRAMH4:	LD	HL,CRAMH5;
	BIT	0,C;
	JR	NZ,CRAMH1;	bude se vyplnovat ctverecek
	LD	A,X'80';
	LD	(DE),A;
	INC	D;
	INC	D;
	LD	A,1;		kresleni prazdneho ctverecku
	JR	CRAMH2;
CRAMH5:	DJNZ	CRAMH4;		jeste neni ctverec hotovej
R5	EQU	$+1;
	LD	HL,CRAMH6;
	JR	CRAMH1;		dno ctverecku
CRAMH6:	LD	A,E;
	SUB	27;
	LD	E,A;		puvodni adresa ctverce
	BIT	0,C;
	JR	NZ,CRAMH7;	ctverecek se vyplnoval
CRAMH9:	EXX;
	JP	(IX);		ne-li navrat
CRAMH7:	LD	A,D;
	ADD	A,5;
	LD	D,A;		kde bude dalsi ctverecek
	JR	CRAMH9;
;
;
TRAMH4:	JR	Z,PORAMH;	test prosel OK
TRAML8:	LD	IY,TRAMH5;
	JP	TRAMH8;		smazat CRT
TRAMH5:	LD	HL,TRAMH6;
	JP	INDIK;
TRAMH6:	LD	IY,POCRT;	a pak znova
	JP	TRAMH7;
;
; Test pameti od adresy 0 do 7FFF ktera je v zakrytu s EPROM a videoRAM. Testovani je
; shodne jako u dolni pulky RAM. Ctverecky signalizujici zdarny prubeh testu se kresli
; do dolni pulky obrazovky. Po chybe se automaticky opakuje test horni pulky RAM.
;
BGRAML	EQU	X'0000';	zacatek 1. pulky RAM
ERAML	EQU	X'7FFF';	konec 1. pulky RAM
	COND	OBJECT EQ EPROM;
BASE	EQU	X'8000';	kam se presune program, jeho baze
	ELSE;
BASE	EQU	X'9000';	kam se presuneme pod CP/M
	ENDC;
STACK	EQU	X'FF00';	vrcholek sklepa
;
PORAMH:	LD	HL,START;	od kud prenaset
	LD	DE,BASE;	kam
	LD	BC,ENDPRG-START;kolik
	LDIR;			tim mame program v horni casti RAM
;
	LD	C,HIGH START;	kdyz jsme se odsunuli, musime prepocist absolutni
	LD	D,HIGH BASE;	adresy v nekterych instrukcich
	LD	B,0+(ETBREL-TBREL)/2;pocet relativizatoru v tabuli
	LD	SP,TBREL;	tabulka co je nutno prepocitat
TRAML1:	POP	IX;
	LD	A,(IX+1);
	SUB	C;		displacement
	ADD	A,D;		kde to bude
	LD	(IX+1),A;
	DJNZ	TRAML1;
;
	JP	TRAML2-START+BASE;skok na nasledujici instrukci ale v presutem useku
;
TRAML2:	LD	DE,BGCRT+128;	kde budou ctverecky - achtung achtung-pracujeme jinde
	EXX;
	LD	IY,PRAML-START+BASE;
	IN	A,(X'30');	odpojit EPROM a videoRAM
	JP	TMEM-START+BASE;provadet test pameti 0-7FFF
;
TRAML3:	LD	IX,TRAML4-START+BASE;
	EX	AF,AF';		uschovat A
	IN	A,(X'50');	pripojit videoRAM
	EX	AF,AF';		obnova A
	JP	TRAMH2-START+BASE;kreslit prazdny/plny ctverecek
TRAML4:	IN	A,(X'30');	odpojit videoRAM
	JP	TMEM2-START+BASE;navrat do testu
;
TRAML6:	EX	AF,AF';		uschova A + F
	IN	A,(X'50');	pripojit EPROM a videoRAM
	EX	AF,AF';		obnova AF
	JP	TRAML7;		skok na nasledujici instrukci ale v EPROM
;
TRAML7:	JP	NZ,TRAML8;	test neprosel dobre
;
; Test osvezovaciho (refresh) cyklu dynamicke pameti RAM. Testuje se pouze videoRAM
; a horni pulka pameti, coz musi stacit (pro blbce: test musi nekde byt ulozen).
; Pamet se popise sachovnici a ceka se asi 10 sekund. Pak se zkontroluje, jestli
; obsah nevyhnil. Chyby se zobrazuji nudlema, a po ni se opakuje od testu horni casti.
;
BGRFR	EQU	BGCRT;		od kud se popisuje
	COND	OBJECT EQ CPM;
EREFR	EQU	X'7FFF';	pod CP/M testovat na refres jen videoram
	ELSE;
EREFR	EQU	X'FFFF';
	ENDC;
;
	LD	IY,PRFR;
	JP	TMEM;		test pameti
;
TRFR2:	BIT	0,A;
	JR	Z,TRFR4;	bude se teprve zapisovat
	EXX;
	LD	D,25;		asi 10 sekund na cekani
	LD	IY,TRFR3;
	JP	WAIT;
;
TRFR3:	EXX;
	LD	IY,PRFR;
	JP	TMEM2;		udelat kontrolu co tam zbylo
;
TRFR4:	LD	A,(DE);		kod co se dela
	CP	X'55';
	JP	Z,TMEM2;	teprve prvni zapis
;
; Test rolovani obrazu. Na televizi se objevi obrazec, ktery roluje chvili nahoru,
; chvili dolu. Spravnou funkci pozorujeme na obrazovce. Pokud se nam to nelibi,
; lze sledovat (i sondou) vyvoj citace na D34 nohy 18-25, nebo na vystupech D85 a D86.
; Tento test je veledulezity, i kdyz vypada jako hracicka. Testuje totiz hlavni data
; bus, jestli tam proudi vsechny bity (pocitame do 255 tak musi). Pokud obraz viditelne
; poskakuje a plynule nejede, je neco blbe. Durazne varuji pred dalsim spoustenim
; jinych testu, neb pri chybe v data busu se spatne naprogramuji 8255 a nechodi
; nic, nebo se to i zakousne a nic se nehne.
;
TROL:	LD	A,X'88';
	OUT	(X'63'),A;	naprogramovani D34 aby chodil roll
	LD	HL,BGCRT;	zacatek videoRAM
	LD	D,64;		kolik je sloupcu
	XOR	A;		nejdriv prazdny ctverecek
TROL0:	LD	C,32;		kolik je toho v sloupci
TROL1:	LD	B,8;
TROL2:	LD	(HL),A;
	INC	HL;
	DJNZ	TROL2;		vykresleni ctverecku
	CPL;
	DEC	C;
	JR	NZ,TROL1;	neni cely sloupec
	CPL;			dalsi sloupec zacit stejne aby byla sachovnice
	DEC	D;
	JR	NZ,TROL0;	neni cela obrazovka
;
	XOR	A;		A := 0
	LD	D,A;		pamatovatko kam rolujeme
	LD	IX,TROL6;
TROL3:	OUT	(X'61'),A;
	LD	C,32;		aby to jelo rychle, ale jeste plynule coz je dulezite
TROL4:	LD	B,255;
TROL5:	DJNZ	TROL5;		zpozdeni, aby to bylo videt
	DEC	C;
	JR	NZ,TROL4;
	JP	(IX);
TROL6:	INC	A;
TROL7:	JR	NZ,TROL3;	neni cele dokola
	CP	D;
	JR	NZ,TROLK;	konec testu
	INC	D;
	LD	IX,TROL8;
	JR	TROL3;
TROL8:	DEC	A;
	JR	TROL7;
;
; Aktivizace minisupervisoru ktery zpracovava klavesnici a ridi dalsi spousteni sekvenci
; testu.
;
TROLK:	DI;			kdyby se to volalo znova po stisku BS a opakovani TROL
	LD	SP,STACK-2;	zpohybnit stack - bacha finta
	LD	IX,TBTST+4;	ktera sekvence se bude delat
	LD	A,HIGH STACK;
	LD	I,A;		naplnit vektor preruseni
	LD	A,X'80';
	LD	(STACK+18),A;	pro SQ 02
	JR	SUPV0;		simulace stisku jine paky nez CR, ROLL nebo BS
;
; Mini supervisor ktery ridi dalsi sequence testu. Stiskem CR nebo ROLL se prechazi na
; dalsi sequenci, stiskem BS na predchozi. Cokoliv jineho opakuje prave bezici sequenci.
; ROLL a BS jsou zvoleny proto, ze maji jen jeden bit a lze je simulovat uzemnovanim
; noh 3 a 6 konektoru klavesnice bez jeji pouziti.
;
SUPV3:	LD	IX,(STACK+16);	ad. adresy bezici sequence
SUPV4:	LD	H,(IX+1);
	LD	L,(IX+0);
	XOR	A;
	CP	H;
	JR	Z,SUPV3;	jsme mimo rozsah, vzit minulou SQ
CNUDL1:	RET;
;
IPKEYB:	DI;
	CALL	SUPV5;
SUPV0:	EX	AF,AF';		uschova znaku z klavesnice pro SQ 01
	CALL	SUPV4
	LD	(STACK+16),IX;	ad. bezici sequence
	POP	AF;		odebere adresu preruseni
	PUSH	HL;		misto toho da ad. sequence
	DEC	HL;
	DEC	HL;		ad. cisla sequence
;
	PUSH	HL;		a uschovat to
	LD	IY,VYPSQ1;
	JP	RAZCRT;		smaze CRT
VYPSQ1:	LD	DE,BGCRT+20+27*256;kde bude text se sequenci
	LD	HL,ZSQ;
	CALL	VYPSQ3;		napise S
	CALL	VYPSQ3;		a Q
	INC	D;		mezera
	POP	IX;
	LD	A,(IX+0);	prvni cislice
	CALL	VYPSQ2;		vypise ji
	LD	A,(IX+1);
	CALL	VYPSQ2;		vypise druhou
	LD	HL,TBIP;
	LD	DE,STACK;
	LD	BC,ETBIP-TBIP;	obnova ad. IP kdyby to sequence znicila
	LDIR;
;
IPIGN2:	LD	B,5;		kolikrat blbnout USART
ZBLBUS:	XOR	A;
	NOP;			aby mel USART dost casu to sezrat
	OUT	(X'41'),A;	zblbnuti USARTu
	DJNZ	ZBLBUS;
	OUT	(X'61'),A;	nula do ROLL registru
	LD	A,X'AE';
	OUT	(X'03'),A;	inicializace D40
	LD	A,00000101B;
	OUT	(X'03'),A;	povolit preruseni od klavesnice (INTEb)
	LD	A,X'88';
	OUT	(X'63'),A;	inicializace D34 (aby nebylo IP od budika)
	LD	A,01001000B;
	OUT	(X'60'),A;	zakazat disk, povolit IP mys
	LD	A,X'90';
	OUT	(X'13'),A;	inicializace 2. citace v D35
	OUT	(X'12'),A;
;
IPIGN:	PUSH	AF;
	LD	A,X'A6';
	OUT	(X'23'),A;	naprogramovani D37
	POP	AF;		protoze IPIGN4 byva take volano z venku
IPIGN4:	PUSH	AF;
	OUT	(X'70'),A;	zruseni IP z RTC
IPIGN3:	LD	A,8;
	OUT	(X'30'),A;	povolit pomoci HW vsechna preruseni
	POP	AF;
	EI;
	RETI;
;
SUPV2:	INC	IX;		stisk CR
	INC	IX;
SUPV1:	POP	BC;		odebrat ze sklepa spojku po volani SUPV5
	JR	SUPV0;
;
; Podprogram na vypis jedne cislice na CRT. A := bin. cislice (0-9), DE := ad. CRT,
; Nici: A, BC, HL. Po ukonceni je v DE ad. dalsi posice v CRT.
;
VYPSQ2:	LD	H,0;
	LD	L,A;
	RLCA;			A * 2
	RLCA;			  * 4
	RLCA;			  * 8
	SUB	L;		  * 7
	LD	L,A;
	LD	BC,ZSQCS;	ad. tabulky znaku cisel
	ADD	HL,BC;
;
; Podprogram na vypis textu na CRT. DE := ad. v CRT, HL := ad. vzorku znaku.
; Nici: A, BC, DE, HL. Po provedeni je DE=ad. dalsiho mista v CRT, HL=ad. dalsiho vzorku.
;
VYPSQ3:	LD	BC,7;		vyska znaku
	LDIR;			presnos znaku
	LD	A,E;
	LD	E,7;
	SUB	E;
	LD	E,A;		couvneme zpet
	INC	D;		v DE je ad. kde bude dalsi znak
	RET;
;
; Podprogram na zjisteni co se zmacklo na klavesnici. Pokud je to CR, BS nebo ROLL,
; tak skoci do supervisoru na zpracovani, jinak se vrati.
; Nici: A, HL, X.
;
SUPV5:	CALL	SUPV3;		sebere adresu bezici SQ
	IN	A,(X'01');	precist znak z klavesnice
	CP	X'F2';
	JR	Z,SUPV2;	stisk paky CR
	CP	X'7F';
	JR	Z,SUPV2;	ROLL tez jako CR
	CP	X'F7';
	RET	NZ;		neni-li BS tak se vratit
	DEC	IX;
	DEC	IX;		couvnout v sequencich
	JR	SUPV1;
;
; Test neocekavaneho IP. Nudlema se zobrazi, od kud prichazi IP. Po jeho propuknuti
; se zinvertuje text SQ 00 a zobrazi nudli odkud vzniklo. V klidu nema zadne IP vzniknout,
; zadne nudle se neukazuji. Jestli to chodi, muzeme vyzkouset uzemnovanim noh D54 nebo
; stiskem nejake klavesy ci kdyz vezmeme do ruky mys.
;
	DB	0,0;
SQ00:	DI;
	LD	DE,STACK;
	LD	HL,SQ00IP;
	LD	BC,ETBIP-TBIP;	delka seznamu rozkoku IP
	LDIR;
	LD	D,0;		nula do citace IP
	EI;
SQ001:	JR	SQ001;
;
SQ002:	DI;
	INC	D;	7 - DATA READY
SQ003:	DI;
	INC	D;	6 - TIMEOUT
SQ004:	DI;
	INC	D;	5 - OVERFLOW
SQ005:	DI;
	INC	D;	4 - RTC
	JR	SQ0061;
SQ006:	DI;
	IN	A,(X'70');	zrusit priznak IP. mys
SQ0061:	INC	D;	3 - MOUSE
SQ007:	DI;
	INC	D;	2 - SIO
SQ008:	DI;
	INC	D;	1 - KEYBOARD
SQ009:	DI;		0 - PRINTER
	LD	A,D;
	CALL	CNUDLE;		zobrazit nudlema
	CALL	INVERT;		invertovat text SQ 00
	LD	D,0;		musi tu byt, kdyby bylo IP furt
	CP	1;
	CALL	Z,SUPV5;	je-li IP z klavesnice zjistit co se zmacklo
	JP	IPIGN2;		neznama paka, navrat
;
; Test klavesnice. Stiskem klavesy se zobrazi jeji hodnota nudlema. Lze testovat i
; uzemnovanim spicek konektoru bez klavesnice.
;
	DB	0,1;
SQ01:	EX	AF,AF';		znak z klavesnice precetl ho supervisor
	CPL;			z klavesnice to prijde inverzni
SQ062:	CALL	CNUDLE;		zobrazit nudlema
SQ011:	JR	SQ011;		vrati se to az po preruseni
;
; Test tiskarny. Posilaji se jednotlive bity, tyto jsou zobrazeny nudlema. Kontrolujeme
; na konektoru tiskarny sondou. Po stisku cehokoliv se pokracuje dalsim bitem.
;
	DB	0,2;
SQ02:	LD	A,(STACK+18);
	RLCA;
	LD	(STACK+18),A;	posunuli jsme se o bit;
	CALL	CNUDLE;
	OUT	(0),A;
SQ021:	JR	SQ021;
;
; Test preruseni systemovych hodin 50 Hz. Nudlema se zobrazuje citac sekund, tam se
; zapocita kazdy 50. impuls.
;
	DB	0,3;
SQ03:	LD	BC,SQ032;
	CALL	SIPRTC;
	LD	B,1;		citac poctu IP
	LD	D,255;		citac sekund
	EI;
SQ031:	JR	SQ031;
;
SIPRTC:	DI;
	LD	(STACK+6),BC;	nova ad. IP od budika
	LD	A,X'04';
	OUT	(X'60'),A;	povolit IP od hodin
	RET;
;
SQ032:	DI;
	DEC	B;
	JP	NZ,IPIGN;	na 50 IP z RTC kaslat
	INC	D;
	LD	A,D;
	CALL	CNUDLE;		zobrazeni citace
	LD	B,51;
	JR	SQ032;
;
; Test mysi. Nudlema se zobrazuje hodnota, ktera se precte z mysi. Dale musi vzniknout
; ÉP kdykoliv na mys sahneme, coz pozname inverzi textu SQ 04 na obrazovce. Lze testovat
; tez pouhym uzemnovanim noh konektoru mysi.
;
	DB	0,4;
SQ04:	DI;
	LD	BC,SQ042;
	LD	(STACK+8),BC;	nova ad. IP z mysi
	EI;
SQ041:	IN	A,(X'70');
	CALL	CNUDLE;
	JR	SQ041;
SQ042:	DI;
	CALL	INVERT;		inverze textu sequence
	IN	A,(X'70');	shodit (mimo jine) priznak IP mysky
	JP	IPIGN;		dodelat IP
;
; Podprogram na inversi textu SQ XY. Nechce nic, neznici nic. Chytrej podprogram.
;
INVERT:	PUSH	HL;
	PUSH	BC;
	PUSH	AF;
	LD	HL,BGCRT+18+26*256;kde je text SQ 03 o jedna pred
	LD	C,7;		kolik bajtu invertovat
INVER1:	LD	B,11;		kolik toho invertovat v bajtu
INVER2:	LD	A,(HL);
	CPL;
	LD	(HL),A;
	INC	L;
	DJNZ	INVER2;
	LD	A,L;
	SUB	11;
	LD	L,A;
	INC	H;		dalsi bajt textu
	DEC	C;
	JR	NZ,INVER1;
	POP	AF;
	POP	BC;
	POP	HL;
	RET;
;
; Test programovych citacu 8253 D35. Na vystupu citace 0 je 1 Hz, na vystupu citace 1
; je 15 Hz, citac 3 se netestuje.  Pokud jsou pochybnosti o jeho funkci, zkontrolujeme
; ho v SQ 08 kde je v cinnosti. Kontrolujeme sondou nebo oscilem na jejich vystupech.
;
	DB	0,5;
SQ05:	LD	A,01000100B;
	OUT	(X'60'),A;	povolit IP od hodin
	LD	A,52;		rezim 2 citace 0
	OUT	(X'13'),A;
	LD	A,50;
	OUT	(X'10'),A;
	XOR	A;
	OUT	(X'10'),A;	citac 0 nastavit na 50
	LD	A,116;		rezim 2 citace 1
	OUT	(X'13'),A;
	LD	A,255;
	OUT	(X'11'),A;
	OUT	(X'11'),A;	citac 1 pocita do 65 535
SQ051:	JR	SQ051;
;
; Test obvodu D27 LS174 pro povoleni diskovych operaci. Po stisku cehokoliv se meni bit
; do tohoto pousteny. Kontrolujeme sondou vsude kam se privadi. Nudlema se ukazuje co
; tam leze.
;
	DB	0,6;
SQ06:	XOR	A;
	OUT	(X'60'),A;	povolit diskove operace
	LD	A,(STACK+18);	bit jako u tiskarny
	AND	00001111B;
	JR	NZ,SQ061;	neni mimo rozsah
	LD	A,X'80';
SQ061:	RLCA;
	LD	(STACK+18),A;	uschovat na priste
	OUT	(X'50'),A;
	JP	SQ062;		ukazat nudle
;
; Test obvodu 8251 D41 usart. Pozorujeme na jeho vystupu jestli tam neco je. Vysila se
; zamnerne co nejpomaleji jen lze, aby to bylo videt.
;
	DB	0,7;
SQ07:	LD	A,118;
	OUT	(X'13'),A;	rezim 3 citace
	LD	A,254;
	OUT	(X'11'),A;
	INC	A;
	OUT	(X'11'),A;	nabiti citace
	LD	A,01000000B;
	OUT	(X'41'),A;	RESET usartu
	LD	A,00111100B;
	NOP;			NOPy jsou tu proto, ze USART je linej a nebere to
	OUT	(X'41'),A;	2 SYN, suda parita, 8 bitu znak
	LD	A,X'55';
	NOP;
	OUT	(X'41'),A;
	NOP;
	NOP;
	OUT	(X'41'),A;	SYN znaky
	DI;
	LD	BC,SQ072;
	LD	(STACK+10),BC;	ad. IP z usartu
	LD	A,X'55';
	LD	A,00110011B;
	OUT	(X'41'),A;	spusteni vysilani
	EI;
SQ071:	JR	SQ071;		cekat IP
SQ072:	LD	A,X'55';
	OUT	(X'40'),A;
	JP	IPIGN;
;
; Test obvodu souvisejicich se zapisem na disk. Kontrolu provadime sondou nebo
; oscilem na D13 noha 3 - pulsy, noha 11 - zapisovana data. Dal musi byt pulsy
; na D54 kde musi proudit IP na nohu 22. Pokud tomu tak neni, je vhodne postupovat
; podle listingu programu a opakovaneho spousteni SQ 08 od zacatku. I kdyz se testuje
; zapis na disk, neni treba mit obavy o diskety, pokud jsou odklopeny hlavicky (a mely
; by byt) coz zkontrolujeme je-li na D44 noha 4 nula.
;
	DB	0,8;
SQ08:	DI;
	CALL	SQ082;		nastaveni potrebneho
	EI;
SQ081:	JR	SQ081;
;
SQ082:	LD	BC,SQ083;
	LD	(STACK),BC;	adr. IP pro DATA READY
	LD	BC,SQ08;
	CALL	SQ101;
SQ0822:	LD	A,00000010B;
	OUT	(X'50'),A;	povolit WRITE
	RET;
;
SQ101:	LD	(STACK+2),BC;	TIME OUT (nemelo by vypuknout)
	LD	(STACK+4),BC;	OVERFLOW (taky by nemelo byt)
	XOR	A;
	OUT	(X'60'),A;	povolit diskove operace
	LD	A,114;
	OUT	(X'13'),A;	rezim 1 citace 1
	LD	A,50;
	OUT	(X'11'),A;
	XOR	A;
	OUT	(X'11'),A;
SQ0821:	LD	A,176;
	OUT	(X'13'),A;	rezim 0 citace 2
	LD	A,255;
	OUT	(X'12'),A;
	LD	A,255;
	OUT	(X'12'),A;
	LD	A,01000000B;
	OUT	(X'60'),A;	zakazat disk. operaci
	XOR	A;
	OUT	(X'60'),A;	hned ji zase povolit = restart citace 1
	LD	A,00001101B;
	OUT	(X'23'),A;	povoleni IP od zapisovanych dat
	LD	A,X'55';
	OUT	(X'20'),A;	dato k zapisu na placku
	RET;
;
SQ083:	DI;
	CALL	SQ0821;
	CALL	SQ0822;
	CALL	IPIGN3;		CALL je tu jako finta, vybira se tam sklep
;
; Test pohybu po diskete a jejiho statusu. Nejprve se provede RESTORE (t.j. nastaveni
; stopy 00), pokud se to nezdari, zobrazi se nudlema FF. Kdyz je na diskete zakaz
; zapisu, je zobrazeno nudlema FE a trvale a sequnce stoji, dokud neni zakaz zapisu
; odstranen. Pak blika v rytmu prijeti index-markru text SQ 09. Na to se krokuje do
; stopy 75 a nudlema se ukazuje kde je hlavicka. Vzdy po peti stopach se priklopi a po
; dalsich peti odklopi hlavicka. Nakonec se udela RESTORE a cela sranda se opakuje pro
; mechaniku B. Je to s podivem, ale obsah disku neni znicen. Ostatne disky ani nemusi
; byt pripojeny, staci sondou sledovat signaly na D34 a uzemnovanim nasimulovat drahu
; 00 a prijeti index-markeru.
;
	DB	0,9;
SQ09:	LD	L,0;		jak je zobrazeno SQ 09
SQ091:	LD	A,00001010B;
	OUT	(X'23'),A;	operace pro mechaniku A
	CALL	SQ09P;		provedeni pohybu
	LD	A,00001011B;
	OUT	(X'23'),A;	mechanika B
	CALL	SQ09P;
	JR	SQ091;
;
SQ09P:	CALL	SQ09R;		provede RESTORE
SQ09P1:	IN	A,(X'62');
	BIT	5,A;
	LD	A,X'FE';
	CALL	NZ,SNUDLE;	je, zakaz zapisu, nudlema FE
	JR	NZ,SQ09P1;
	LD	B,15;		kolik petic stop pojedem
	LD	D,00000011B;	prikaz pro disk
	LD	H,0;		citac stop
SQ09P4:	LD	C,5;		kdy priklopit/odklopit hlavu
SQ09P3:	LD	A,H;
	CALL	SNUDLE;		zobrazit stopu
	LD	A,D;
	OUT	(X'62'),A;
	INC	H;		pricist stopu
	LD	E,80;		zpozdeni
SQ09P5:	CALL	SQ09W1;		chvilku pockat aby se mechanika nestrhala
	DEC	E;
	JR	NZ,SQ09P5;
	DEC	C;
	JR	NZ,SQ09P3;
	LD	A,D;
	XOR	8;		priklopit/odklopit hlavu
	LD	D,A;
	DJNZ	SQ09P4;
	CALL	SQ09R;		udelat restore
	RET;
;
SQ09R:	LD	B,80;		podprogram na RESTORE - kdy uz ma byt stopa 00
	LD	D,00000001B;
SQ09R1:	LD	A,D;
	OUT	(X'62'),A;	pohyb hlavy
	CALL	SQ09W;
	IN	A,(X'62');
	BIT	4,A;
	RET	NZ;		je stopa 00
	DJNZ	SQ09R1;
	LD	A,255;		neni stopa 00 a uz davno mela byt
	CALL	SNUDLE;
	JR	SQ09R
;
SNUDLE:	PUSH	BC;		zobrazeni nudli bez smazani registru
	PUSH	AF;
	PUSH	HL;
	CALL	CNUDLE;
	POP	HL;
	POP	AF;
	POP	BC;
	RET;
;
SQ09W:	PUSH	DE;		podprogram na vyckani a indikaci index-markeru
	LD	E,100;
	CALL	SQ09I;		zobrazit IM a pockat
	POP	DE;
SQ09W1:	PUSH	DE;
	LD	A,D;
	XOR	1;
	OUT	(X'62'),A;	puls pro pohyb hlavy
	LD	E,255;
	CALL	SQ09I;		opet pockat a zobrazit IM
	POP	DE;
	RET;
;
SQ09I:	IN	A,(X'62');	podprogram na pockani a v cekani zobrazuje IM
	BIT	7,A;
	JR	NZ,SQ09I2;	je IM
	BIT	0,L;
	JR	Z,SQ09I1;	neni a nebyl
	RES	0,L;		ze nebyl
SQ09I1:	DEC	E;
	JR	NZ,SQ09I;
	RET;
SQ09I2:	BIT	0,L;
	JR	NZ,SQ09I1;	je IM a byl
	SET	0,L;		ze byl
	CALL	INVERT;
	JR	SQ09I1;
;
; Test kompletniho diskoveho radice. Protoze ale temer vse je jiz otestovano, jedna se
; vlastne o test ctecich obvodu. Pri odklopenych hlavickach (aby se nic nestalo diskum)
; se formatuje a cte zaroven, cimz data prosakuji na vstup a lze je sledovat.
; Teoreticky by to slo i bez mechanik, kdyby se propojil vystup se vstupem a signal T43
; se propojil s index-markrem (generuji zde pulsy). Ale je to tam s otevrenym kolektorem,
; tak nevim, musi se asi udelat nejaky forrichtung. Tim je cely pocitac otestovan.
; Zdar, zdar, zdar, nazdaaaaaaaaaaaaaaaar.
;
	DB	1,0;
SQ10:	DI;
	LD	BC,SQ103;
	LD	(STACK),BC;	ad. IP pro DATA READY
	LD	BC,SQ10;	ad. IP OVERFLOW + TIME OUT
	CALL	SQ101;		nastavit vse potrebne
	CALL	SQ104;
	LD	B,25;		po kolika IP z RTC simulovat IM
	EI;
SQ102:	JR	SQ102;
;
SQ104:	LD	A,X'55';
	OUT	(X'20'),A;	data k zapisu na disk
	LD	A,00010001B;
	OUT	(X'50'),A;	povoleni FORMAT + READ
	LD	BC,SQ105;
	LD	(STACK+6),BC;	ad. IP z RTC
	LD	A,00000100B;
	OUT	(X'60'),A;	povolit diskove operace a IP z RTC
	RET;
;
SQ103:	DI;
	IN	A,(X'21');	kdyby se neco precetlo
	CALL	SQ0821;
	CALL	SQ104;
	CALL	IPIGN3;
;
SQ105:	DI;
	DEC	B;
	JP	NZ,IPIGN4;	25 IP z RTC ignorovat
	LD	A,00001000B;
	OUT	(X'62'),A;	udelat puls ktery pujde do IM
	LD	B,20;
SQ106:	DJNZ	SQ106;		aby chvili trval
	XOR	A;
	OUT	(X'62'),A;	konec pulsu IM
	LD	B,26;
	JR	SQ105;
;
; Protoze zbylo kousicek mista v EPROM, tak srandicka na konec.
;
	DB	1,1;
SQ11:	LD	HL,ENDCRT;
	LD	C,0;
SQ111:	LD	B,64;
SQ112:	CALL	SQ117;
	LD	(HL),A;
	DEC	H;
	LD	A,110;
SQ114:	DEC	A;
	JR	NZ,SQ114;
	DJNZ	SQ112;
	LD	H,HIGH ENDCRT;
	DEC	L;
	DEC	C;
	JR	NZ,SQ111;
;
SQ115:	CALL	SQ117;
	LD	(BGCRT),A;
	LD	HL,ENDCRT-1;
	LD	DE,ENDCRT;
	LD	BC,ENDCRT-BGCRT;
	LDDR;
	LD	B,100;
SQ116:	DJNZ	SQ116;
	JR	SQ115;
;
SQ117:	IN	A,(X'11');
	CP	0;
	JR	Z,SQ117;
	RET;
;
;
;
ZSQ:	DB	3CH,42H,40H,3CH,02H,42H,3CH,3CH,42H,42H,42H,4AH,46H,3EH;
ZSQCS:	DB	3CH,42H,42H,4AH,52H,42H,3CH;
	DB	08H,18H,28H,48H,08H,08H,08H;
	DB	3CH,42H,02H,04H,08H,10H,3EH;
	DB	3EH,42H,04H,0CH,02H,42H,3CH;
	DB	10H,20H,40H,44H,7EH,04H,04H;
	DB	7CH,40H,40H,7CH,02H,42H,3CH;
	DB	3CH,42H,40H,5CH,62H,42H,3CH;
	DB	7EH,02H,02H,04H,08H,10H,20H;
	DB	3CH,42H,42H,3CH,42H,42H,3CH;
	DB	3CH,42H,46H,3AH,02H,42H,3CH;
TBIP:	DEFW	IPIGN,IPIGN,IPIGN,IPIGN,IPIGN,IPIGN,IPKEYB,IPIGN;
ETBIP	EQU	$;
SQ00IP:	DEFW	SQ002,SQ003,SQ004,SQ005,SQ006,SQ007,SQ008,SQ009;
TBTST:	DEFW	0,TROL,SQ00,SQ01,SQ02,SQ03,SQ04,SQ05,SQ06,SQ07,SQ08,SQ09,SQ10,SQ11,0;
TBMEM:	DB	55H,0FEH,0AAH,01H,55H,0FEH,0AAH,01H,55H,0FEH,0AAH,01H,0FFH;
TCRTP:	DB	HIGH BGCRT,HIGH ENDCRT;
	DEFW	TCRT2,TMEM2;
PRAMH:	DB	HIGH BGRAMH,HIGH ERAMH;
	DEFW	TRAMH4,TRAMH2;
TBREL:	DEFW	R1-START+BASE,R2-START+BASE;
	DEFW	R3-START+BASE,R4-START+BASE,R5-START+BASE;
ETBREL	EQU	$;
PRAML:	DB	HIGH BGRAML,HIGH ERAML;
	DEFW	TRAML6-START+BASE,TRAML3-START+BASE;
PRFR:	DB	HIGH BGRFR,HIGH EREFR;
	DEFW	TRAML8,TRFR2;
ENDPRG	EQU	$;
	END;
