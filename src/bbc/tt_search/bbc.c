#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef WIN64
    #include <windows.h>
#else
    # include <sys/time.h>
#endif


/**************

MACROS

***************/
//Shortcuts voor later: #define [naam] [betekenis (waar de shortcut voor staat)].
#define U64 unsigned long long
#define set_bit(bitboard, square) ((bitboard) |= (1ULL << (square)))
#define get_bit(bitboard, square) ((bitboard) & (1ULL << (square)))
#define pop_bit(bitboard, square) ((bitboard) &= ~(1ULL << (square)))

//Nuttige posities voor debuggen.
#define empty_board "8/8/8/8/8/8/8/8 w - - "
#define start_position "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 "
#define tricky_position "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1 "
#define killer_position "rnbqkb1r/pp1p1pPp/8/2p1pP2/1P1P4/3P3P/P1P1P3/RNBQKBNR w KQkq e6 0 1"
#define dumb_ai "1rq2b1N/p1p1k1p1/7p/3Qn3/3P4/P7/1PP2PPP/R1B1R1K1 w - - 3 20 "


/**************

BOARD ENUM

***************/
//Rij voor namen van vakjes op het spelbord. Hiermee kan je de computer vertellen print ... of vakje c4 en dan doet die dat. (en mogelijkheden voor kleur/stukken die met lijnen gaan).
enum {
    a8, b8, c8, d8, e8, f8, g8, h8,
    a7, b7, c7, d7, e7, f7, g7, h7,
    a6, b6, c6, d6, e6, f6, g6, h6,
    a5, b5, c5, d5, e5, f5, g5, h5,
    a4, b4, c4, d4, e4, f4, g4, h4,
    a3, b3, c3, d3, e3, f3, g3, h3,
    a2, b2, c2, d2, e2, f2, g2, h2,
    a1, b1, c1, d1, e1, f1, g1, h1, no_sq
};

//Mogelijkheden voor soorten bitboards.
enum { white, black, both };
//Sliding pieces.
enum { rook, bishop };

//Kleuren en kant voor mogelijke toegestane casteling (schaakmove) aangegeven in bits. Hierbij staat w voor white, b voor black, q voor queenside en k voor kingside.
enum { wk = 1, wq = 2, bk = 4, bq = 8 };
//Alle mogelijke stukken in het spel met hoofdletters voor witte stukken.
enum { P, N, B, R, Q, K, p, n, b, r, q, k };

//Vakje naar coördinaten: voor vertaling van computertaal naar mensentaal.
const char* square_to_coordinates[] = {
    "a8", "b8", "c8", "d8", "e8", "f8", "g8", "h8",
    "a7", "b7", "c7", "d7", "e7", "f7", "g7", "h7",
    "a6", "b6", "c6", "d6", "e6", "f6", "g6", "h6",
    "a5", "b5", "c5", "d5", "e5", "f5", "g5", "h5",
    "a4", "b4", "c4", "d4", "e4", "f4", "g4", "h4",
    "a3", "b3", "c3", "d3", "e3", "f3", "g3", "h3",
    "a2", "b2", "c2", "d2", "e2", "f2", "g2", "h2",
    "a1", "b1", "c1", "d1", "e1", "f1", "g1", "h1",
};

//Ascii: American Standard Code for Information Interchange. Geen flauw idee.
char ascii_pieces[] = "PNBRQKpnbrqk";

//Van integer naar character dus van P naar 'P'. Dit is handig voor het printen en debuggen.
int char_pieces[] = {
    ['P'] = P,
    ['N'] = N,
    ['B'] = B,
    ['R'] = R,
    ['Q'] = Q,
    ['K'] = K,
    ['p'] = p,
    ['n'] = n,
    ['b'] = b,
    ['r'] = r,
    ['q'] = q,
    ['k'] = k
};

//Aantal bitboards per stuk, p,r,n,b,q,k voor wit en zwart is 12.
U64 bitboards[12];
//Aantal bitboards voor wit, zwart en beide.
U64 occupancies[3];

//Game states: Kant, enpassant mogelijkheid en castling mogelijkheid.
int side;
int enpassant = no_sq;
int castle;

// pseudo random position identifiers (naast collisions)
U64 hash_key;

/**************

Time control variabelen

***************/

// exit from engine flag
int quit = 0;

// UCI "movestogo" command moves counter
int movestogo = 30;

// UCI "movetime" command time counter
int movetime = -1;

// UCI "time" command holder (ms)
int time = -1;

// UCI "inc" command's time increment holder
int inc = 0;

// UCI "starttime" command time holder
int starttime = 0;

// UCI "stoptime" command time holder
int stoptime = 0;

// variable to flag time control availability
int timeset = 0;

// variable to flag when the time is up
int stopped = 0;


/**************

Miscellaneous functions

***************/

// get time in milliseconds
int get_time_ms()
{
    #ifdef WIN64
        return GetTickCount();
    #else
        struct timeval time_value;
        gettimeofday(&time_value, NULL);
        return time_value.tv_sec * 1000 + time_value.tv_usec / 1000;
    #endif
}

/*
  Function to "listen" to GUI's input during search.
  It's waiting for the user input from STDIN.
  OS dependent.

  First Richard Allbert aka BluefeverSoftware grabbed it from somewhere...
  And then Code Monkey King has grabbed it from VICE)

*/

int input_waiting()
{
#ifndef WIN32
    fd_set readfds;
    struct timeval tv;
    FD_ZERO(&readfds);
    FD_SET(fileno(stdin), &readfds);
    tv.tv_sec = 0; tv.tv_usec = 0;
    select(16, &readfds, 0, 0, &tv);

    return (FD_ISSET(fileno(stdin), &readfds));
#else
    static int init = 0, pipe;
    static HANDLE inh;
    DWORD dw;

    if (!init)
    {
        init = 1;
        inh = GetStdHandle(STD_INPUT_HANDLE);
        pipe = !GetConsoleMode(inh, &dw);
        if (!pipe)
        {
            SetConsoleMode(inh, dw & ~(ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT));
            FlushConsoleInputBuffer(inh);
        }
    }

    if (pipe)
    {
        if (!PeekNamedPipe(inh, NULL, 0, NULL, &dw, NULL)) return 1;
        return dw;
    }

    else
    {
        GetNumberOfConsoleInputEvents(inh, &dw);
        return dw <= 1 ? 0 : dw;
    }

#endif
}

// read GUI/user input
void read_input()
{
    // bytes to read holder
    int bytes;

    // GUI/user input
    char input[256] = "", *endc;

    // "listen" to STDIN
    if (input_waiting())
    {
        // tell engine to stop calculating
        stopped = 1;

        // loop to read bytes from STDIN
        do
        {
            // read bytes from STDIN
            bytes = read(fileno(stdin), input, 256);
        }

        // until bytes available
        while (bytes < 0);

        // searches for the first occurrence of '\n'
        endc = strchr(input, '\n');

        // if found new line set value at pointer to 0
        if (endc) *endc = 0;

        // if input is available
        if (strlen(input) > 0)
        {
            // match UCI "quit" command
            if (!strncmp(input, "quit", 4))
            {
                // tell engine to terminate exacution
                quit = 1;
            }

            // // match UCI "stop" command
            else if (!strncmp(input, "stop", 4)) {
                // tell engine to terminate exacution
                quit = 1;
            }
        }
    }
}

// a bridge function to interact between search and GUI input
static void communicate() {
    // if time is up break here
    if (timeset == 1 && get_time_ms() > stoptime) {
        // tell engine to stop calculating
        stopped = 1;
    }

    // read GUI input
    read_input();
}


/**************

RANDOMS
Dit geeft altijd dezelfde nummers in dezelfde volgorde want gaat volgens een algortime.

***************/
//Pseudo random number state, lijkt random maar is volgens een algoritme (Xor shift).
unsigned int state = 1804289383;

//Maak een 32-bit pseudo random number.
unsigned int get_random_U32_number()
{
    //Krijg state die het op dat moment is.
    unsigned int number = state;

    //Xor shift algorithm. (32-bit pseudo random generator algorithm)
    //Number = number ^ leftshift met 13 bits.
    number ^= number << 13;
    //Number = number ^ rightshift met 17 bits.
    number ^= number >> 17;
    //Number = number ^ leftshift met 5 bits.
    number ^= number << 5;

    //Update number naar state.
    state = number;

    return number;
}

//Maak een 64-bit pseudo random number.
U64 get_random_U64_number()
{
    //Namen voor 4 verschillende random nummers.
    U64 n1, n2, n3, n4;

    //Geef de 4 random nummers waardes en "slice" ze naar 16-bit.
    n1 = (U64)(get_random_U32_number()) & 0xFFFF;
    n2 = (U64)(get_random_U32_number()) & 0xFFFF;
    n3 = (U64)(get_random_U32_number()) & 0xFFFF;
    n4 = (U64)(get_random_U32_number()) & 0xFFFF;

    //Zet ze achter elkaar en niet op elkaar (elke random nummer bestaat uit 16 bits).
    return n1 | (n2 << 16) | (n3 << 32) | (n4 << 48);
}

//Maak magic number mogelijkheden.
U64 generate_magic_number()
{
    //Alleen de bits die bij alle 3 de random nummers 1 zijn.
    return get_random_U64_number() & get_random_U64_number() & get_random_U64_number();
}


/**************

BIT MANIP

***************/

//Telt het aantal bits op het bitboard dat aan(1) staat.
static inline int count_bits(U64 bitboard)
{
    int count = 0;
    while (bitboard)
    {
        count++;
        bitboard &= bitboard - 1;
    }
    return count;
}

//Pak de eerste bit. Van linksboven naar rechtsonder. (ls1b = last significant 1st bit).
static inline int get_ls1b_index(U64 bitboard)
{
    //Als bitboard niet gelijk is aan 0.
    if (bitboard)
    {
        return count_bits((bitboard & (~bitboard + 1)) - 1);
    }
    //Als bitboard = 0.
    else return -1;
}
/**************

ZOBRIST

***************/
// random piece keys [soort stuk][positie(vakjes)]
U64 piece_keys[12][64];

//random enpassant keys [positie(vakjes)]
U64 enpassant_keys[64];

//Random castling keys
U64 castle_keys[16];

//random side key
U64 side_key;

//init de random hask keys
void init_random_keys()
{
    //update pseudo random number state
    state = 1804289383;

    //loop over piece codes
    for (int piece = P; piece <= k; piece++)
    {
        // loop over board squares
        for (int square = 0; square < 64; square++)
        //init random piece keys
        piece_keys[piece][square] = get_random_U64_number();

    }
    // loop over board squares
    for (int square = 0; square < 64; square++)
        //init random enpassant keys
        enpassant_keys[square] = get_random_U64_number();

    //init random side key
    side_key = get_random_U64_number();

    //loop over castling keys
    for (int index = 0; index < 16; index++)
        //init castle keys
        castle_keys[index] = get_random_U64_number();
}
//generate pseudorandom position identifier (hash key) From scratch
U64 generate_hash_key()
{
    //final hash key
    U64 final_key = 0ULL;

    // bitboard copy
    U64 bitboard;

    //loop over piece bit boards
    for (int piece = P; piece <= k; piece++)
    {
        //init piece bitboard copy
        bitboard = bitboards[piece];

        //loop over pieces binnen bitbaord
        while (bitboard)
        {
            //init square occupied by piece
            int square = get_ls1b_index(bitboard);

            //hash piece (add piece to key)
            final_key ^= piece_keys[piece][square];

            //pop ls1b
            pop_bit(bitboard,square);
        }
    }
    //if enpassant square is op bord
    if (enpassant != no_sq)
        //hash enpassant
        final_key ^= enpassant_keys[enpassant];

    //hash castling rights
    final_key ^= castle_keys[castle];

    //hash the side only if black een zet moet doen
    if (side == black) final_key ^= side_key;

    //return deze hash_key
    return final_key;
}

/**************

INPUT & OUTPUT

***************/
void print_bitboard(U64 bitboard)
{
    printf("\n");
    //Herhaald 9x voor het aantal horizontale rijen (0, 1, 2, 3, 4, 5, 6, 7, 8).
    for (int rank = 0; rank < 8; rank++)
    {
        //Herhaald 8x voor het aantal verticale rijen voor nu (0, 1, 2, 3, 4, 5, 6, 7, 8) maar eigenlijk (a, b, c, d, e, f, g, h). Dan zijn alle vakjes geweest want je hebt 8x8 + een rij onder en links om de naam van het vakje aan te geven.
        for (int file = 0; file < 8; file++)
        {
            //Benoemd het nummer van het vakje van linksboven naar rechtsonder. Linksboven dus 1, daarna 2 en als laatste 64 rechtsonder.
            int square = rank * 8 + file;
            //Als het de eerste verticale rij is (rij 0), dan print van 1 tot 8 (dit is de rij links van het spelbord die aangeeft op welke rij je zit).
            if (file == 0)
            {
                printf("  %d ", 8 - rank);
            }
            //Print de bit status van een vakje, dit is of 1 of 0.
            printf(" %d", get_bit(bitboard, square) ? 1 : 0);
        }
        //Witregel.
        printf("\n");
    }
    //Print kolom letters onderaan.
    printf("\n     a b c d e f g h\n\n");
    printf("     Bitboard: %llu\n\n", bitboard);
}

//Print board.
void print_board()
{
    //Nieuwe regel.
    printf("\n");
    //Loop ranks.
    for (int rank = 0; rank < 8; rank++)
    {
        //Loop files.
        for (int file = 0; file < 8; file++)
        {
            //Geef vakjes nummers van 1 tot 64
            int square = rank * 8 + file;

            //Print 1 tot 9 links van het bord voor rijen.
            if (!file)
            {
                printf("  %d ", 8 - rank);
            }

            //Deffinieer variabele piece.
            int piece = -1;

            //Voor alle stukken, kijken welk stuk het is.
            for (int bitboard_piece = P; bitboard_piece <= k; bitboard_piece++)
            {
                if (get_bit(bitboards[bitboard_piece], square)) piece = bitboard_piece;
            }

            //Als geen stuk, dan ".", als wel een stuk, print het stuk op een 8x8 veld.
            printf(" %c", (piece == -1) ? '.' : ascii_pieces[piece]);
        }
        //Nieuwe regel.
        printf("\n");
    }
    //Print a tot h onderaan.
    printf("\n     a b c d e f g h\n\n");
    //Print kant aan zet.
    printf("  Side:        %s\n", !side ? "White" : "Black");
    //Print enpassant mogelijheden.
    printf("  Enpassant:   %s\n", (enpassant != no_sq) ? square_to_coordinates[enpassant] : "No");
    //Print castling mogelijkheden.
    printf("  Castling:    %c%c%c%c\n\n",
        (castle & wk) ? 'K' : '-',
        (castle & wq) ? 'Q' : '-',
        (castle & bk) ? 'k' : '-',
        (castle & bq) ? 'q' : '-');
}

//Fen naar board.
void parse_fen(const char* fen)
{
    //Reset bitboard naar alles 0.
    memset(bitboards, 0ULL, sizeof(bitboards));
    //Reset occupancies
    memset(occupancies, 0ULL, sizeof(occupancies));
    //Reset game states/variables.
    side = 0;
    enpassant = no_sq;
    castle = 0;

    //Loop over elk vakje en teken in de fen dat geen spatie is.
    for (int square = 0; square < 64 && *fen && *fen != ' ';)
    {
        //Als een stuk, dan...
        if ((*fen >= 'b' && *fen <= 'r') || (*fen >= 'B' && *fen <= 'R'))
        {
            //Kijken welk stuk (decoden).
            int piece = char_pieces[*fen];
            //Zet een bit op het goede bitboard van dat stuk op de goede plek.
            set_bit(bitboards[piece], square);
            //Volgende vakje.
            square++;
            //Volgende teken in fen.
            fen++;
        }
        //Anders als er een cijfer staat:
        else if (*fen >= '1' && *fen <= '8')
        {
            //Stel offset gelijk aan cijfer dat in de fen staat.
            int offset = *fen - '0';
            //Ga het aantal offset vakjes verder.
            square += offset;
            //Volgende teken in fen.
            fen++;
        }
        //Alle overigen:
        else
        {
            //Volgende teken in fen.
            fen++;
        }
    }

    //Weer volgende teken in fen.
    fen++;
    //Wie is aan zet.
    (*fen == 'w') ? (side = white) : (side = black);
    //Twee tekens overslaan in fen.
    fen += 2;
    //Zolang fen =geen spatie, kijk naar castlingmogelijkheden.
    while (*fen != ' ')
    {
        switch (*fen)
        {
        case 'K': castle |= wk; break;
        case 'Q': castle |= wq; break;
        case 'k': castle |= bk; break;
        case 'q': castle |= bq; break;
        case '-': break;
        }
        //Volgende teken in fen.
        fen++;
    }
    //Volgende teken in fen.
    fen++;
    //Als teken in fen geen streepje is:
    if (*fen != '-')
    {
        //Geef het vakje aan dat enpassant kan worden geslagen.
        int file = fen[0] - 'a';
        int rank = 8 - (fen[1] - '0');
        enpassant = rank * 8 + file;
    }
    //Anders:
    else
    {
        enpassant = no_sq;
    }

    //Occupancies voor wit.
    for (int piece = P; piece <= K; piece++) occupancies[white] |= bitboards[piece];
    //Occupancies voor zwart.
    for (int piece = p; piece <= k; piece++) occupancies[black] |= bitboards[piece];
    //Occupancies voor beide.
    occupancies[both] |= occupancies[white];
    occupancies[both] |= occupancies[black];

    //init hash key
    hash_key = generate_hash_key();
}

/**************

ATTACKS

***************/
//Bitboard voor niet de a kolom.
const U64 not_a_file = 18374403900871474942ULL;
//Bitboard voor niet de h kolom.
const U64 not_h_file = 9187201950435737471ULL;
//Bitboard voor niet de g of h kolom.
const U64 not_gh_file = 4557430888798830399ULL;
//Bitboard voor niet de a of b kolom.
const U64 not_ab_file = 18229723555195321596ULL;

//Aantal relevant vakjes dat de bishop aanvalt op elk vakje (zijden zijn niet relevant tenzij de bishop op een zijkant staat).
const int bishop_relevant_bits[64] = {
    6, 5, 5, 5, 5, 5, 5, 6,
    5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 7, 7, 7, 7, 5, 5,
    5, 5, 7, 9, 9, 7, 5, 5,
    5, 5, 7, 9, 9, 7, 5, 5,
    5, 5, 7, 7, 7, 7, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5,
    6, 5, 5, 5, 5, 5, 5, 6
};

//Aantal relevant vakjes dat de rook aanvalt op elk vakje (zijden zijn niet relevant tenzij de rook op een zijkant staat).
const int rook_relevant_bits[64] = {
    12, 11, 11, 11, 11, 11, 11, 12,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    12, 11, 11, 11, 11, 11, 11, 12
};

//rook magic numbers
U64 rook_magic_numbers[64] = {
    0x8a80104000800020ULL,
    0x140002000100040ULL,
    0x2801880a0017001ULL,
    0x100081001000420ULL,
    0x200020010080420ULL,
    0x3001c0002010008ULL,
    0x8480008002000100ULL,
    0x2080088004402900ULL,
    0x800098204000ULL,
    0x2024401000200040ULL,
    0x100802000801000ULL,
    0x120800800801000ULL,
    0x208808088000400ULL,
    0x2802200800400ULL,
    0x2200800100020080ULL,
    0x801000060821100ULL,
    0x80044006422000ULL,
    0x100808020004000ULL,
    0x12108a0010204200ULL,
    0x140848010000802ULL,
    0x481828014002800ULL,
    0x8094004002004100ULL,
    0x4010040010010802ULL,
    0x20008806104ULL,
    0x100400080208000ULL,
    0x2040002120081000ULL,
    0x21200680100081ULL,
    0x20100080080080ULL,
    0x2000a00200410ULL,
    0x20080800400ULL,
    0x80088400100102ULL,
    0x80004600042881ULL,
    0x4040008040800020ULL,
    0x440003000200801ULL,
    0x4200011004500ULL,
    0x188020010100100ULL,
    0x14800401802800ULL,
    0x2080040080800200ULL,
    0x124080204001001ULL,
    0x200046502000484ULL,
    0x480400080088020ULL,
    0x1000422010034000ULL,
    0x30200100110040ULL,
    0x100021010009ULL,
    0x2002080100110004ULL,
    0x202008004008002ULL,
    0x20020004010100ULL,
    0x2048440040820001ULL,
    0x101002200408200ULL,
    0x40802000401080ULL,
    0x4008142004410100ULL,
    0x2060820c0120200ULL,
    0x1001004080100ULL,
    0x20c020080040080ULL,
    0x2935610830022400ULL,
    0x44440041009200ULL,
    0x280001040802101ULL,
    0x2100190040002085ULL,
    0x80c0084100102001ULL,
    0x4024081001000421ULL,
    0x20030a0244872ULL,
    0x12001008414402ULL,
    0x2006104900a0804ULL,
    0x1004081002402ULL
};

//bishop magic numbers
U64 bishop_magic_numbers[64] = {
    0x40040844404084ULL,
    0x2004208a004208ULL,
    0x10190041080202ULL,
    0x108060845042010ULL,
    0x581104180800210ULL,
    0x2112080446200010ULL,
    0x1080820820060210ULL,
    0x3c0808410220200ULL,
    0x4050404440404ULL,
    0x21001420088ULL,
    0x24d0080801082102ULL,
    0x1020a0a020400ULL,
    0x40308200402ULL,
    0x4011002100800ULL,
    0x401484104104005ULL,
    0x801010402020200ULL,
    0x400210c3880100ULL,
    0x404022024108200ULL,
    0x810018200204102ULL,
    0x4002801a02003ULL,
    0x85040820080400ULL,
    0x810102c808880400ULL,
    0xe900410884800ULL,
    0x8002020480840102ULL,
    0x220200865090201ULL,
    0x2010100a02021202ULL,
    0x152048408022401ULL,
    0x20080002081110ULL,
    0x4001001021004000ULL,
    0x800040400a011002ULL,
    0xe4004081011002ULL,
    0x1c004001012080ULL,
    0x8004200962a00220ULL,
    0x8422100208500202ULL,
    0x2000402200300c08ULL,
    0x8646020080080080ULL,
    0x80020a0200100808ULL,
    0x2010004880111000ULL,
    0x623000a080011400ULL,
    0x42008c0340209202ULL,
    0x209188240001000ULL,
    0x400408a884001800ULL,
    0x110400a6080400ULL,
    0x1840060a44020800ULL,
    0x90080104000041ULL,
    0x201011000808101ULL,
    0x1a2208080504f080ULL,
    0x8012020600211212ULL,
    0x500861011240000ULL,
    0x180806108200800ULL,
    0x4000020e01040044ULL,
    0x300000261044000aULL,
    0x802241102020002ULL,
    0x20906061210001ULL,
    0x5a84841004010310ULL,
    0x4010801011c04ULL,
    0xa010109502200ULL,
    0x4a02012000ULL,
    0x500201010098b028ULL,
    0x8040002811040900ULL,
    0x28000010020204ULL,
    0x6000020202d0240ULL,
    0x8918844842082200ULL,
    0x4010011029020020ULL
};

//Pawn attack table[kant][vakje].
U64 pawn_attacks[2][64];
//Paard attack table[vakje].
U64 knight_attacks[64];
//Koning attack table[vakje].
U64 king_attacks[64];

//Bishop attack masks[vakje].
U64 bishop_masks[64];
//Rook attack masks[vakje].
U64 rook_masks[64];

//Bishop attack table[vakje][occupencies].
U64 bishop_attacks[64][512];
//Rook attack table[vakje][occupencies].
U64 rook_attacks[64][4096];

//Alle pion moves (met zwart/wit en welk vakje).
U64 mask_pawn_attacks(int side, int square)
{
    //idk.
    U64 attacks = 0ULL;
    U64 bitboard = 0ULL;

    //Een stuk op het bord zetten.
    set_bit(bitboard, square);

    //Als wit aan zet.
    if (!side)
    {
        //Vakje +7 is het vakje linksboven en vakje +9 is het vakje rechtsboven. Dat zijn de plekken die een witte pion kan aanvallen. Not_a_file en not_h_file zijn omdat de pion niet naar links kan slaan als die al helemaal links staat.
        if ((bitboard >> 7) & not_a_file) attacks |= (bitboard >> 7);
        if ((bitboard >> 9) & not_h_file) attacks |= (bitboard >> 9);
    }
    //Als zwart aan zet.
    else
    {
        //Vakje -7 is het vakje rechtsonder en vakje -9 is het vakje linksboven. Dat zijn de plekken die een witte pion kan aanvallen. Not_a_file en not_h_file zijn omdat de pion niet naar links kan slaan als die al helemaal links staat.
        if ((bitboard << 7) & not_h_file) attacks |= (bitboard << 7);
        if ((bitboard << 9) & not_a_file) attacks |= (bitboard << 9);
    }
    //Return alle attacks.
    return attacks;
}

//Alle paard moves.
U64 mask_knight_attacks(int square)
{
    //idk.
    U64 attacks = 0Ull;
    U64 bitboard = 0ULL;

    //Een stuk op het bord zetten.
    set_bit(bitboard, square);

    //Alle kanten dat een paard op kan. Hierbij staan de nummers: 17, 15, 10 en 6 voor plekken omhoog en omlaag. Linksboven is hierbij 1 en rechtsonder plek 64.
    if ((bitboard >> 17) & not_h_file) attacks |= (bitboard >> 17);
    if ((bitboard >> 15) & not_a_file) attacks |= (bitboard >> 15);
    if ((bitboard >> 10) & not_gh_file) attacks |= (bitboard >> 10);
    if ((bitboard >> 6) & not_ab_file) attacks |= (bitboard >> 6);
    if ((bitboard << 17) & not_a_file) attacks |= (bitboard << 17);
    if ((bitboard << 15) & not_h_file) attacks |= (bitboard << 15);
    if ((bitboard << 10) & not_ab_file) attacks |= (bitboard << 10);
    if ((bitboard << 6) & not_gh_file) attacks |= (bitboard << 6);

    //Return alle attacks.
    return attacks;
}

//Alle koning moves.
U64 mask_king_attacks(int square)
{
    //idk
    U64 attacks = 0ULL;
    U64 bitboard = 0ULL;

    //Een stuk op het bord zetten.
    set_bit(bitboard, square);

    //Alle kanten dat een koning op kan. Hierbij staan de nummers: 8, 9, 7 en 1 voor plekken omhoog en omlaag. Linksboven is hierbij 1 en rechtsonder plek 64.
    if (bitboard >> 8) attacks |= (bitboard >> 8);
    if ((bitboard >> 9) & not_h_file) attacks |= (bitboard >> 9);
    if ((bitboard >> 7) & not_a_file) attacks |= (bitboard >> 7);
    if ((bitboard >> 1) & not_h_file) attacks |= (bitboard >> 1);
    if (bitboard << 8) attacks |= (bitboard << 8);
    if ((bitboard << 9) & not_a_file) attacks |= (bitboard << 9);
    if ((bitboard << 7) & not_h_file) attacks |= (bitboard << 7);
    if ((bitboard << 1) & not_a_file) attacks |= (bitboard << 1);

    //Return alle attacks.
    return attacks;
}
//Alle bishop moves.
U64 mask_bishop_attacks(int square)
{
    U64 attacks = 0ULL;

    //Ranks (r) & files (f).
    int r, f;

    //Init target Ranks (r) & files (f).
    int tr = square / 8;
    int tf = square % 8;

    //Waar kan bishop staan. Hierbij staan de nummers voor plekken waar de bishop naar toe kan gaan (r<=6 zodat hij niet uit het bord gaat). Linksboven is hierbij 1 en rechtsonder plek 64.
    for (r = tr + 1, f = tf + 1; r <= 6 && f <= 6; r++, f++) attacks |= (1ULL << (r * 8 + f));
    for (r = tr - 1, f = tf + 1; r >= 1 && f <= 6; r--, f++) attacks |= (1ULL << (r * 8 + f));
    for (r = tr + 1, f = tf - 1; r <= 6 && f >= 1; r++, f--) attacks |= (1ULL << (r * 8 + f));
    for (r = tr - 1, f = tf - 1; r >= 1 && f >= 1; r--, f--) attacks |= (1ULL << (r * 8 + f));

    return attacks;
}
//Alle rook moves.
U64 mask_rook_attacks(int square)
{
    //Resultaat van aanval.
    U64 attacks = 0ULL;

    //Ranks & files.
    int r, f;

    //Target ranks & files.
    int tr = square / 8;
    int tf = square % 8;

    //Waar kan rook staan.
    for (r = tr + 1; r <= 6; r++) attacks |= (1ULL << (r * 8 + tf));
    for (r = tr - 1; r >= 1; r--) attacks |= (1ULL << (r * 8 + tf));
    for (f = tf + 1; f <= 6; f++) attacks |= (1ULL << (tr * 8 + f));
    for (f = tf - 1; f >= 1; f--) attacks |= (1ULL << (tr * 8 + f));

    return attacks;
}
//Generate bishop attacks on the fly (zijkanten niet skippen).
U64 bishop_attacks_on_the_fly(int square, U64 block)
{
    U64 attacks = 0ULL;

    //Ranks & files.
    int r, f;

    //Init target ranks & files.
    int tr = square / 8;
    int tf = square % 8;

    //Generate bishop attacks.
    for (r = tr + 1, f = tf + 1; r <= 7 && f <= 7; r++, f++)
    {
        attacks |= (1ULL << (r * 8 + f));
        if ((1ULL << (r * 8 + f)) & block) break;
    }
    for (r = tr - 1, f = tf + 1; r >= 0 && f <= 7; r--, f++)
    {
        attacks |= (1ULL << (r * 8 + f));
        if ((1ULL << (r * 8 + f)) & block) break;
    }
    for (r = tr + 1, f = tf - 1; r <= 7 && f >= 0; r++, f--)
    {
        attacks |= (1ULL << (r * 8 + f));
        if ((1ULL << (r * 8 + f)) & block) break;
    }
    for (r = tr - 1, f = tf - 1; r >= 0 && f >= 0; r--, f--)
    {
        attacks |= (1ULL << (r * 8 + f));
        if ((1ULL << (r * 8 + f)) & block) break;
    }

    return attacks;
}

//Generate rook attacks on the fly (zijkanten niet skippen).
U64 rook_attacks_on_the_fly(int square, U64 block)
{
    //Result attacks.
    U64 attacks = 0ULL;

    //Ranks & files.
    int r, f;
    int tr = square / 8;
    int tf = square % 8;

    //Generate rook attacks.
    for (r = tr + 1; r <= 7; r++)
    {
        attacks |= (1ULL << (r * 8 + tf));
        if ((1ULL << (r * 8 + tf)) & block) break;
    }
    for (r = tr - 1; r >= 0; r--)
    {
        attacks |= (1ULL << (r * 8 + tf));
        if ((1ULL << (r * 8 + tf)) & block) break;
    }
    for (f = tf + 1; f <= 7; f++)
    {
        attacks |= (1ULL << (tr * 8 + f));
        if ((1ULL << (tr * 8 + f)) & block) break;
    }
    for (f = tf - 1; f >= 0; f--)
    {
        attacks |= (1ULL << (tr * 8 + f));
        if ((1ULL << (tr * 8 + f)) & block) break;
    }

    return attacks;
}

void init_leapers_attacks()
{
    //Loop alle 64 vakjes zodat elk vakje is geweest.
    for (int square = 0; square < 64; square++)
    {
        //Mask elk vakje dat wordt aangevallen door een pion appart voor elk vakje van wit.
        pawn_attacks[white][square] = mask_pawn_attacks(white, square);
        //Mask elk vakje dat wordt aangevallen door een pion appart voor elk vakje van zwart.
        pawn_attacks[black][square] = mask_pawn_attacks(black, square);
        //Mask elk vakje dat wordt aangevallen door een paard appart voor elk vakje.
        knight_attacks[square] = mask_knight_attacks(square);
        //Mask elk vakje dat wordt aangevallen door een koning appart voor elk vakje.
        king_attacks[square] = mask_king_attacks(square);
    }
}

//Alle mogelijke manieren om een attack mask te hebben.
U64 set_occupancy(int index, int bits_in_mask, U64 attack_mask)
{
    U64 occupancy = 0ULL;

    //Loop het aantal keer als bits in een mask.
    for (int count = 0; count < bits_in_mask; count++)
    {
        //Square = eerste bit van linksboven naar rechtsonder.
        int square = get_ls1b_index(attack_mask);

        //Weghalen bit.
        pop_bit(attack_mask, square);

        //Lefshift 1 met count.
        if (index & (1 << count))
        {
            occupancy |= (1ULL << square);
        }
    }
    //return occupancy
    return occupancy;
}


/*****************************************\

                  MAGICS

\*****************************************/
//Het vinden van magic number
U64 find_magic_number(int square, int relevant_bits, int bishop)
{
    //init occupancy array
    U64 occupancies[4096];

    //init attack tables
    U64 attacks[4096];

    //init used attacks
    U64 used_attacks[4096];


    //Als attack_mask = bishop, dan mask_bishop_attacks. Anders mask_rook_attacks.
    U64 attack_mask = bishop ? mask_bishop_attacks(square) : mask_rook_attacks(square);

    //init occupancy indicies
    int occupancy_indicies = 1 << relevant_bits;

    //loop over occupancy indicies (welke occupancies zijn mogelijk)
    for (int index = 0; index < occupancy_indicies; index++)
    {
        //init occupancies
        occupancies[index] = set_occupancy(index, relevant_bits, attack_mask);

        //init attacks: Als attacks[index] = bishop, dan bishop attacks, anders rook attacks.
        attacks[index] = bishop ? bishop_attacks_on_the_fly(square, occupancies[index]) : rook_attacks_on_the_fly(square, occupancies[index]);
    }

    //test magic numbers loop
    for (int random_count = 0; random_count < 100000000; random_count++)
    {
        // generate magic number canditate --> na tests moet het blijken of het de echte is
        U64 magic_number = generate_magic_number();

        // skip inappropriate magic numebrs
        if (count_bits((attack_mask * magic_number) & 0xFF00000000000000) < 6) continue;

        //init used attacks
        memset(used_attacks, 0ULL, sizeof(used_attacks));

        //init index & fail flag
        int index, fail;

        //test magic index loop
        for (index = 0, fail = 0; !fail && index < occupancy_indicies; index++)
        {
            //init magic index
            int magic_index = (int)((occupancies[index] * magic_number) >> (64 - relevant_bits));

            //if magic index works
            if (used_attacks[magic_index] == 0ULL)
                //init used attacks
                used_attacks[magic_index] = attacks[index];
            //otherwise
            else if (used_attacks[magic_index] != attacks[index])
                //magic index doesnt work
                fail = 1;
        }
        //if magic number works -->
        if (!fail)
            return magic_number;
    }
    //if magic number doesnt work
    printf("  Magic number fails!");
    return 0ULL;
}
//init magic numbers
void init_magic_numbers()
{
    // loop over 64 bit squares
    for (int square = 0; square < 64; square++)
    {
        //init rook magic numbers
        printf("0x%llxULL\n", find_magic_number(square, rook_relevant_bits[square], rook));
    }
    // duidelijk verschil tussen magic numbers
    printf("\n\n\n");
    for (int square = 0; square < 64; square++)
    {
        printf("0x%llxULL\n", find_magic_number(square, bishop_relevant_bits[square], bishop));
    }
}
//Init sliders attack table.
void init_sliders_attacks(int bishop)
{
    //Loop over alle 64 vakjes.
    for (int square = 0; square < 64; square++)
    {
        //Init bishop & rook masks.
        bishop_masks[square] = mask_bishop_attacks(square);
        rook_masks[square] = mask_rook_attacks(square);

        //Init current mask. Als bishop, dan bishop masks. Als geen bishop, dan rook masks.
        U64 attack_mask = bishop ? bishop_masks[square] : rook_masks[square];

        //Init occupancy_indicies.
        int occupancy_indicies = (1 << count_bits(attack_mask));

        //Loop alle occupency indicies.
        for (int index = 0; index < occupancy_indicies; index++)
        {
            //Bishop
            if (bishop)
            {
                //Init current occupency
                U64 occupancy = set_occupancy(index, bishop_relevant_bits[square], attack_mask);
                //Init current index
                int magic_index = (occupancy * bishop_magic_numbers[square]) >> (64 - bishop_relevant_bits[square]);
                //Init bishop attacks
                bishop_attacks[square][magic_index] = bishop_attacks_on_the_fly(square, occupancy);
            }
            //rook
            else
            {
                //Init current occupency
                U64 occupancy = set_occupancy(index, rook_relevant_bits[square], attack_mask);
                //Init current index
                int magic_index = (occupancy * rook_magic_numbers[square]) >> (64 - rook_relevant_bits[square]);
                //Init rook attacks
                rook_attacks[square][magic_index] = rook_attacks_on_the_fly(square, occupancy);
            }
        }
    }
}

//Bishop attacks krijgen. Dit zijn nu echt de bits die de bishop aanvalt.
static inline U64 get_bishop_attacks(int square, U64 occupancy)
{
    //Als er een bishop op een vakje staat.
    occupancy &= bishop_masks[square];
    //Vakje maal magic number.
    occupancy *= bishop_magic_numbers[square];
    //Bits rightshifted met 64-aantal relevant bits.
    occupancy >>= 64 - bishop_relevant_bits[square];

    //Krijg bishop attacks
    return bishop_attacks[square][occupancy];
}

//Toren attacks krijgen. Dit zijn nu echt de bits die de toren aanvalt.
static inline U64 get_rook_attacks(int square, U64 occupancy)
{
    //Als er een toren op een vakje staat
    occupancy &= rook_masks[square];
    //Vakje maal magic number.
    occupancy *= rook_magic_numbers[square];
    //Bits rightshifted met 64-aantal relevant bits.
    occupancy >>= 64 - rook_relevant_bits[square];

    //Krijg toren attacks.
    return rook_attacks[square][occupancy];
}

//Koningin attacks krijgen. Dit zijn nu echt de bits die de toren aanvalt.
static inline U64 get_queen_attacks(int square, U64 occupancy)
{
    //Krijg toren en bishop attacks want koningin kan wat een toren en bishop kan.
    return (get_bishop_attacks(square, occupancy) | get_rook_attacks(square, occupancy));
}


/**************

Encoding moves

***************/

/* Dit zijn het aantal bits dat nodig is per zet of slaan en ook in hexidecimal. Er zijn bv 64 source squares. 6 bits zorgen daarvoor (2^6=64),
verder ook 64 target squares. 12 verschillende stukken wit en zwart samen, hiervoor zijn 4 bits nodig, 2^4 = 16 maar 2^3 is te weinig. etc.

          binary move bits                               hexidecimal constants

    0000 0000 0000 0000 0011 1111    source square       0x3f
    0000 0000 0000 1111 1100 0000    target square       0xfc0
    0000 0000 1111 0000 0000 0000    piece               0xf000
    0000 1111 0000 0000 0000 0000    promoted piece      0xf0000
    0001 0000 0000 0000 0000 0000    capture flag        0x100000
    0010 0000 0000 0000 0000 0000    double push flag    0x200000
    0100 0000 0000 0000 0000 0000    enpassant flag      0x400000
    1000 0000 0000 0000 0000 0000    castling flag       0x800000
*/

//Encode move. Backslash omdat meerdere lines code bij elkaar horen??
#define encode_move(source, target, piece, promoted, capture, doublep, enpassant, castling) \
    (source << 0) | (target << 6) | (piece << 12) | (promoted << 16) | (capture << 20) | (doublep << 21) | (enpassant << 22) | (castling << 23)
    //<<6 is 6 naar links want normaal begint het rechts en de 1tjes staan 6 naar links voor target zoals hierboven te zien. Voor de rest ook zo. Vid 27 extra uitleg.

//Define shortcuts voor later, de move & hexidecimal staat voor de 1en aan het begin van dit hoofdstuk "encoding moves". Vid 27 extra uitleg.
//"&" is een bitwise operation voor alleen 1 als beide 1 zijn. Hierdoor krijg je alleen de source bij source en niet andere informatie erbij.
#define get_move_source(move) (move & 0x3f)
#define get_move_target(move) ((move & 0xfc0) >> 6)
#define get_move_piece(move) ((move & 0xf000) >> 12)
#define get_move_promoted(move) ((move & 0xf0000) >> 16)
#define get_move_capture(move) (move & 0x100000)
#define get_move_double(move) (move & 0x200000)
#define get_move_enpassant(move) (move & 0x400000)
#define get_move_castling(move) (move & 0x800000)

//movelist structure
typedef struct {
    //moves
    int moves[256];
    // Move count (ofwel het bijhouden van index van de move)
    int count;

} moves;

//Toevoegen van moves aan de movelist.
static inline void add_move(moves* move_list, int move)
{
    //Store moves.
    move_list->moves[move_list->count] = move;
    //Vergroot move count.
    move_list->count++;
}

//Promoted pieces, voor wit en zwart beide kleine letters.
char promoted_pieces[] = {
    [Q] = 'q',
    [R] = 'r',
    [B] = 'b',
    [N] = 'n',
    [q] = 'q',
    [r] = 'r',
    [b] = 'b',
    [n] = 'n'
};

//Print move (UCI purposes).
void print_move(int move)
{
    //Als promoted, print met promotion.
    if (get_move_promoted(move))
        printf("%s%s%c",
            square_to_coordinates[get_move_source(move)],
            square_to_coordinates[get_move_target(move)],
            promoted_pieces[get_move_promoted(move)]);
    //Anders, print zonder promotion.
    else
        printf("%s%s",
            square_to_coordinates[get_move_source(move)],
            square_to_coordinates[get_move_target(move)]);
}
//Print move list (voor debuggen).
void print_move_list(moves* move_list)
{
    //Do niks bij een lege move list.
    if (!move_list->count)
    {
        //Print geen zetten gevonden.
        printf("\n  No moves found!\n\n");
        return;
    }
    printf("\n  move    piece   capture   double    enpass    castling\n\n");
    //loop over moves within move list
    for (int move_count = 0; move_count < move_list->count; move_count++)
    {
        //init move
        int move = move_list->moves[move_count];
        //print move
        printf("  %s%s%c   %c       %d         %d         %d         %d\n",
            square_to_coordinates[get_move_source(move)],
            square_to_coordinates[get_move_target(move)],
            get_move_promoted(move) ? promoted_pieces[get_move_promoted(move)] : ' ',
            ascii_pieces[get_move_piece(move)],
            get_move_capture(move) ? 1 : 0,
            get_move_double(move) ? 1 : 0,
            get_move_enpassant(move) ? 1 : 0,
            get_move_castling(move) ? 1 : 0);
    }

    //Print aantal zetten
    printf("\n\n     Total number of moves: %d\n\n", move_list->count);

}

#define copy_board()                                                        \
    U64 bitboards_copy[12], occupancies_copy[3];                            \
    int side_copy, enpassant_copy, castle_copy;                             \
    memcpy(bitboards_copy, bitboards, 96);                                  \
    memcpy(occupancies_copy, occupancies, 24);                              \
    side_copy = side, enpassant_copy = enpassant, castle_copy = castle;     \
    U64 hash_key_copy = hash_key;                                           \

#define take_back()                                                         \
    memcpy(bitboards, bitboards_copy, 96);                                  \
    memcpy(occupancies, occupancies_copy, 24);                              \
    side = side_copy, enpassant = enpassant_copy, castle = castle_copy;     \
    hash_key = hash_key_copy                                                \


/**************

Move Gen

***************/
//Of een vak wordt aangevallen door de gegeven kant (of wit of zwart): belangrijk voor snelheid, want illegale moves worden niet berekend
static inline int is_square_attacked(int square, int side)
{
    //De vakken die worden aangevallen door witte pion
    if ((side == white) && (pawn_attacks[black][square] & bitboards[P])) return 1;
    //De vakken die worden aangevallen door zwarte pion
    if ((side == black) && (pawn_attacks[white][square] & bitboards[p])) return 1;
    //De vakken die worden aangevallen door paard wit of zwart
    if (knight_attacks[square] & ((!side) ? bitboards[N] : bitboards[n])) return 1;
    //De vakken die worden aangevallen door loper wit en zwart
    if (get_bishop_attacks(square, occupancies[both]) & ((!side) ? bitboards[B] : bitboards[b])) return 1;
    //De vakken die worden aangevallen door toren wit of zwart
    if (get_rook_attacks(square, occupancies[both]) & ((!side) ? bitboards[R] : bitboards[r])) return 1;
    //De vakken die worden aangevallen door koningin wit of zwart
    if (get_queen_attacks(square, occupancies[both]) & ((!side) ? bitboards[Q] : bitboards[q])) return 1;

    //De vakken die worden aangevallen door koning (wit/zwart)
    if (king_attacks[square] & ((!side) ? bitboards[K] : bitboards[k])) return 1;
    //Als niks het aanvalt
    return 0;
}
//Het printen van de aangevallen vakjes.
void print_attacked_squares(int side)
{
    //Nieuwe regel.
    printf("\n");
    //Loop over de rijen.
    for (int rank = 0; rank < 8; rank++)
    {
        //Loop over bord kollommen.
        for (int file = 0; file < 8; file++)
        {
            //Convert f&r --> square.
            int square = rank * 8 + file;
            //Print files (als file =/ 0, dan -->
            if (!file)
            {
                printf("  %d  ", 8 - rank);
            }
            //Checken of het vakje wordt aangevallen.
            printf("%d ", is_square_attacked(square, side) ? 1 : 0);
        }
        //Nieuwe lijn elke rij.
        printf("\n");
    }
    //Print rijen.
    printf("\n     a b c d e f g h\n\n");
}

//Soorten zetten.
enum { all_moves, only_captures };

//Castling rights.
const int castling_rights[64] = {
     7, 15, 15, 15,  3, 15, 15, 11,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    13, 15, 15, 15, 12, 15, 15, 14
};

//
static inline int make_move(int move, int move_flag)
{
    //Zet zonder slaan.
    if (move_flag == all_moves)
    {
        //Kopieer bord.
        copy_board();

        //Alles dat bij een zet komt kijken.
        int source_square = get_move_source(move);
        int target_square = get_move_target(move);
        int piece = get_move_piece(move);
        int promoted = get_move_promoted(move);
        int capture = get_move_capture(move);
        int double_push = get_move_double(move);
        int enpass = get_move_enpassant(move);
        int castling = get_move_castling(move);

        //Verzet een stuk
        //Maak een bit een 0.
        pop_bit(bitboards[piece], source_square);
        //Maak een bit een 1.
        set_bit(bitboards[piece], target_square);

        //Hash piece (remove piece from source square and move to target square)
        hash_key ^= piece_keys[piece][source_square];
        hash_key ^= piece_keys[piece][target_square];

        //Als iets geslagen wordt.
        if (capture)
        {
            //Variables.
            int start_piece, end_piece;
            //Als wit aan zet.
            if (side == white)
            {
                //Loop zwarte stukken
                start_piece = p;
                end_piece = k;
            }
            //Als zwart aan zet.
            else
            {
                //Loop witte stukken
                start_piece = P;
                end_piece = K;
            }
            //Loop stukken op bord.
            for (int bb_piece = start_piece; bb_piece <= end_piece; bb_piece++)
            {
                //Als een stuk op het aangevallen vakje staat moet die worden verwijderd (wordt geslagen).
                if (get_bit(bitboards[bb_piece], target_square))
                {
                    //Verwijder bit (slaan).
                    pop_bit(bitboards[bb_piece], target_square);

                    //Remove the piece from hash key
                    hash_key ^= piece_keys[bb_piece][target_square];
                    break;
                }
            }
        }
        //Als promotie.
        if (promoted)
        {
            //Verwijder pion als wit dan P, als zwart dan p.
            pop_bit(bitboards[(side == white) ? P : p], target_square);

            //Als wit aan zet
            if(side == white)
            {
                //Verwijder pion
                pop_bit(bitboards[P], target_square);

                //Verwijder pion van hash key
                hash_key ^= piece_keys[P][target_square];
            }

            //Als zwart aan zet
            else
            {
                //Verwijder pion
                pop_bit(bitboards[p], target_square);

                //Verwijder pion van hash key
                hash_key ^= piece_keys[p][target_square];
            }

            //Zet een nieuw stuk neer (meestal koningin, het promoted piece).
            set_bit(bitboards[promoted], target_square);

            //Voeg de promoted_piece toe aan de hash key
            hash_key ^= piece_keys[promoted][target_square];
        }
        //Als enpassant.
        if (enpass)
        {
            (side == white) ?
                //Als wit, popbit +8.
                pop_bit(bitboards[p], target_square + 8) :
                //Als zwart, popbit -8.
                pop_bit(bitboards[P], target_square - 8);

                //Wit aan zet
                if (side==white)
                {
                    //Verwijder captured pawn
                    pop_bit(bitboards[p], target_square + 8);

                    //Verwijder pion van hash key
                    hash_key ^= piece_keys[p][target_square + 8];
                }

                //Zwart aan zet
                else
                {
                    //Verwijder captured pawn
                    pop_bit(bitboards[P], target_square - 8);

                    //Verwijder pion van hash key
                    hash_key ^= piece_keys[P][target_square - 8];
                }
        }

        //hash enpassant (verwijder enpassant square van hash key)
        if (enpassant != no_sq) hash_key ^= enpassant_keys[enpassant];

        //Zet enpassant naar niks want na een double pawn push wordt het een vakje en een zet verder kan dat niet meer.
        enpassant = no_sq;

        //Double pawn push.
        if (double_push)
        {
                /*(side == white) ?
                //Als wit aan zet is, dan enpassant vakje +8.
                (enpassant = target_square + 8) :
                //Als zwart aan zet is, dan enpassant vakje -8.
                (enpassant = target_square - 8);*/

                //Wit aan zet
                if (side==white)
                {
                    //Set enpassent square
                    enpassant = target_square + 8;

                    //hash enpassent
                    hash_key ^= enpassant_keys[target_square + 8];
                }

                //Zwart aan zet
                else
                {
                    //Set enpassent square
                    enpassant = target_square - 8;

                    //hash enpassent
                    hash_key ^= enpassant_keys[target_square - 8];
                }
        }
        //Castling
        if (castling)
        {
            //Target_square
            switch (target_square)
            {
                //Als target_square = g1.
            case (g1):
                //Verwijder toren op h1 en zet op f1
                pop_bit(bitboards[R], h1);
                set_bit(bitboards[R], f1);

                //Hash rook
                hash_key ^= piece_keys[R][h1];
                hash_key ^= piece_keys[R][f1];
                break;
                //Als target_square = c1.
            case (c1):
                //Verwijder toren op a1 en zet op d1
                pop_bit(bitboards[R], a1);
                set_bit(bitboards[R], d1);

                //Hash rook
                hash_key ^= piece_keys[R][a1];
                hash_key ^= piece_keys[R][d1];
                break;
                //Als target_square = g8.
            case (g8):
                //Verwijder toren op h8 en zet op f8
                pop_bit(bitboards[r], h8);
                set_bit(bitboards[r], f8);

                //Hash rook
                hash_key ^= piece_keys[R][h8];
                hash_key ^= piece_keys[R][f8];
                break;
                //Als target_square = c8.
            case (c8):
                //Verwijder toren op a8 en zet op d8
                pop_bit(bitboards[r], a8);
                set_bit(bitboards[r], d8);

                //Hash rook
                hash_key ^= piece_keys[R][a8];
                hash_key ^= piece_keys[R][d8];
                break;
            }
        }

        //Hash castling
        hash_key ^= castle_keys[castle];

        //Castle mag alleen als toren en koning nog niet hebben bewogen.
        castle &= castling_rights[source_square];
        castle &= castling_rights[target_square];

        //Hash castling
        hash_key ^= castle_keys[castle];

        //Copy occupancies.
        memset(occupancies, 0ULL, 24);
        //Loop over witte stukken bitboards en update white occupancies.
        for (int bb_piece = P; bb_piece <= K; bb_piece++) occupancies[white] |= bitboards[bb_piece];
        //Loop over witte stukken bitboards en update black occupancies.
        for (int bb_piece = p; bb_piece <= k; bb_piece++) occupancies[black] |= bitboards[bb_piece];
        //Update beide
        occupancies[both] |= occupancies[white];
        occupancies[both] |= occupancies[black];

        //Switch side
        side ^= 1;

        //Side switched dus ook hash switched
        hash_key ^= side_key;

        // ===== debug hash key incremental update ===== //

        //Maak een hask key voor updated positie
        U64 hash_from_scratch = generate_hash_key();

        //Als de hash key niet overeenkomt met een incrementaly updated hask key, dan stop even.
        if (hash_key != hash_from_scratch)
        {
            //voor debuggen
            printf("\n\nMake move\n");
            printf("move: "); print_move(move);
            print_board();
            printf("hash key should be: %llx\n", hash_from_scratch);
            getchar();
        }

        //Als koning wordt aangevallen door andere kleur.
        if (is_square_attacked((side == white) ? get_ls1b_index(bitboards[k]) : get_ls1b_index(bitboards[K]), side))
        {
            //Take back.
            take_back();
            //Illegal move.
            return 0;
        }
        //Legal move
        else return 1;
    }
    //Als de zet iets aanvalt.
    else
    {
        //Zeker weten dat de zet een aanval is.
        if (get_move_capture(move)) make_move(move, all_moves);
        //Anders niet doen.
        else return 0;
    }
}
// generate all moves
static inline void generate_moves(moves* move_list)
{
    //Init move count.
    move_list->count = 0;
    //definieer waar het stuk staat en waar het naartoe gaat
    int source_square, target_square;

    // definieer hoe het bitboard van het huidige stuk eruitziet & z'n aanvallen en maak er een kopie die wordt geloopt (meer info: vid 23 12:00=18:00)
    U64 bitboard, attacks;

    //Loop over alle stukken
    for (int piece = P; piece <= k; piece++)
    {
        //Init stuk bitboard kopie.
        bitboard = bitboards[piece];
        //Generate witte pionnen moves en witte koning rokeer moves (vid 23+)
        if (side == white)
        {
            //Als stuk is pion.
            if (piece == P)
            {
                //Loop over witte pawns op het bitboard.
                while (bitboard)
                {
                    //Waar een pion staat.
                    source_square = get_ls1b_index(bitboard);
                    //Waar een pion naartoe kan.
                    target_square = source_square - 8;

                    //Als het vakje niet op het bord staat en er geen ander stuk staat.
                    if (!(target_square < a8) && !get_bit(occupancies[both], target_square))
                    {
                        //Als het vakje op de laatste rij is, dan promotie.
                        if (source_square >= a7 && source_square <= h7)
                        {
                            //Voeg de zet toe aan de mogelijke zetten (Promoties).
                            add_move(move_list, encode_move(source_square, target_square, piece, Q, 0, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, R, 0, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, B, 0, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, N, 0, 0, 0, 0));
                        }

                        //Als vakje niet op laatste rij.
                        else
                        {
                            //Voeg de zet toe aan de mogelijke zetten (Pion 1 vakje naar voren).
                            add_move(move_list, encode_move(source_square, target_square, piece, 0, 0, 0, 0, 0));

                            //Voeg de zet toe aan de mogelijke zetten (Pion 2 vakjes naar voren).
                            if ((source_square >= a2 && source_square <= h2) && !get_bit(occupancies[both], target_square - 8))
                                //Voeg de zet toe aan de mogelijke zetten.
                                add_move(move_list, encode_move(source_square, target_square - 8, piece, 0, 0, 1, 0, 0));
                        }
                    }
                    //Attacks = pawn attacks van wit EN occupancies van black.
                    attacks = pawn_attacks[side][source_square] & occupancies[black];

                    //Als de pion iets aanvalt
                    while (attacks)
                    {
                        //Waar een pion naartoe kan.
                        target_square = get_ls1b_index(attacks);

                        //Als pion op laatste rij, dan promotie.
                        if (source_square >= a7 && source_square <= h7)
                        {
                            //Voeg de zet toe aan de mogelijke zetten (Aanval+promotie).
                            add_move(move_list, encode_move(source_square, target_square, piece, Q, 1, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, R, 1, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, B, 1, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, N, 1, 0, 0, 0));
                        }
                        //Als pion niet op laatste rij.
                        else
                        {
                            //Voeg ze zet toe aan de mogelijke zetten (Aanvallen stuk).
                            add_move(move_list, encode_move(source_square, target_square, piece, 0, 1, 0, 0, 0));
                        }
                        //Haal de pion weg zodat de zetten voor volgende pion kunnen worden berekend.
                        pop_bit(attacks, target_square);
                    }

                    //Als enpassant = een vakje.
                    if (enpassant != no_sq)
                    {
                        //enpassant attacks = pawn attacks EN enpassant vakje.
                        U64 enpassant_attacks = pawn_attacks[side][source_square] & (1ULL << enpassant);
                        //Als enpassant attack kan.
                        if (enpassant_attacks)
                        {
                            //Enpassant slaan.
                            int target_enpassant = get_ls1b_index(enpassant_attacks);
                            //Voeg de zet toe aan de mogelijke zetten (Enpassant slaan).
                            add_move(move_list, encode_move(source_square, target_enpassant, piece, 0, 1, 0, 1, 0));
                        }
                    }
                    //Haal de least significant bit weg.
                    pop_bit(bitboard, source_square);
                }
            }

            //Castling voor wit.
            if (piece == K)
            {
                //Kingside castling.
                if (castle & wk)
                {
                    //De vakjes ertussen zijn niet bezet door een stuk.
                    if (!get_bit(occupancies[both], f1) && !get_bit(occupancies[both], g1))
                    {
                        //De vakjes ertussen worden niet aangevallen door zwart.
                        if (!is_square_attacked(e1, black) && !is_square_attacked(f1, black))
                        {
                            //Voeg de zet toe aan de mogelijke zetten (Kingside castling).
                            add_move(move_list, encode_move(e1, g1, piece, 0, 0, 0, 0, 1));
                        }
                    }
                }
                //Queenside castling.
                if (castle & wq)
                {
                    //De vakjes ertussen zijn niet bezet door een stuk.
                    if (!get_bit(occupancies[both], b1) && !get_bit(occupancies[both], c1) && !get_bit(occupancies[both], d1))
                    {
                        //De vakjes ertussen worden niet aangevallen door zwart.
                        if (!is_square_attacked(d1, black) && !is_square_attacked(e1, black))
                        {
                            //Voeg de zet toe aan de mogelijke zetten (Queenside castling).
                            add_move(move_list, encode_move(e1, c1, piece, 0, 0, 0, 0, 1));
                        }
                    }
                }
            }
        }


        //Generate zwarte pion bewegingen en zwarte koning rokeer moves.
        else
        {
            //Als stuk is pion.
            if (piece == p)
            {
                //Loop alle stukken op het bitboard.
                while (bitboard)
                {
                    //Waar een pion staat.
                    source_square = get_ls1b_index(bitboard);
                    //Waar een pion naartoe kan.
                    target_square = source_square + 8;

                    //Als het vakje niet op het bord staat en er geen ander stuk staat.
                    if (!(target_square > h1) && !get_bit(occupancies[both], target_square))
                    {
                        //Als het vakje op de laatste rij staat, dan promotie.
                        if (source_square >= a2 && source_square <= h2)
                        {
                            //Voeg de zet toe aan de mogelijke zetten (Promotie).
                            add_move(move_list, encode_move(source_square, target_square, piece, q, 0, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, r, 0, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, b, 0, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, n, 0, 0, 0, 0));
                        }
                        //Als pion niet op 1na laatste rij staat
                        else
                        {
                            //Voeg de zet toe aan de mogelijke zetten (Pion 1 vakje naar voren).
                            add_move(move_list, encode_move(source_square, target_square, piece, 0, 0, 0, 0, 0));

                            //Als op 2e rij, kan ook 2 vakjes naar voren.
                            if ((source_square >= a7 && source_square <= h7) && !get_bit(occupancies[both], target_square + 8))
                                //Voeg de zet toe aan de mogelijke zetten (Pion 2 vakjes naar voren).
                                add_move(move_list, encode_move(source_square, target_square + 8, piece, 0, 0, 1, 0, 0));
                        }
                    }

                    //Attacks = pawn attacks van zwart EN occupancies van wit.
                    attacks = pawn_attacks[side][source_square] & occupancies[white];

                    //Als de pion iets aanvalt
                    while (attacks)
                    {
                        //Target square = eerste vakje dat aangevallen wordt vanaf linksboven.
                        target_square = get_ls1b_index(attacks);

                        //Als pion op laatste rij, dan promotie.
                        if (source_square >= a2 && source_square <= h2)
                        {
                            //Voeg de zet toe aan de mogelijke zetten (Aanvallen+promotie)
                            add_move(move_list, encode_move(source_square, target_square, piece, q, 1, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, r, 1, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, b, 1, 0, 0, 0));
                            add_move(move_list, encode_move(source_square, target_square, piece, n, 1, 0, 0, 0));
                        }
                        //Als pion niet op laatste rij.
                        else
                        {
                            //Voeg de zet toe aan de mogelijke zetten (Slaan)
                            add_move(move_list, encode_move(source_square, target_square, piece, 0, 1, 0, 0, 0));
                        }

                        //Haal de pion weg zodat de zetten voor volgende pion kunnen worden berekend.
                        pop_bit(attacks, target_square);
                    }

                    //Als enpassant = een vakje.
                    if (enpassant != no_sq)
                    {
                        //enpassant attacks = pawn attacks EN enpassant vakje.
                        U64 enpassant_attacks = pawn_attacks[side][source_square] & (1ULL << enpassant);
                        //Als enpassant attacks kan.
                        if (enpassant_attacks)
                        {
                            //Enpassant slaan.
                            int target_enpassant = get_ls1b_index(enpassant_attacks);
                            //Voeg de zet toe aan de mogelijke zetten (Enpassant slaan).
                            add_move(move_list, encode_move(source_square, target_enpassant, piece, 0, 1, 0, 1, 0));
                        }
                    }
                    //Haal de least significant bit weg.
                    pop_bit(bitboard, source_square);
                }
            }

            //Castling voor zwart.
            if (piece == k)
            {
                //Kingside castling.
                if (castle & bk)
                {
                    //De vakjes ertussen zijn niet bezet door een stuk.
                    if (!get_bit(occupancies[both], f8) && !get_bit(occupancies[both], g8))
                    {
                        //De vakjes ertussen worden niet aangevallen door zwart.
                        if (!is_square_attacked(e8, white) && !is_square_attacked(f8, white))
                        {
                            //Voeg de zet toe aan de mogelijke zetten.
                            add_move(move_list, encode_move(e8, g8, piece, 0, 0, 0, 0, 1));
                        }
                    }
                }
                //Queenside castling.
                if (castle & bq)
                {
                    //De vakjes ertussen zijn niet bezet door een stuk.
                    if (!get_bit(occupancies[both], b8) && !get_bit(occupancies[both], c8) && !get_bit(occupancies[both], d8))
                    {
                        //De vakjes ertussen worde nniet aangevallen door zwart.
                        if (!is_square_attacked(d8, white) && !is_square_attacked(e8, white))
                        {
                            //Voeg de zet toe aan de mogelijke zetten.
                            add_move(move_list, encode_move(e8, c8, piece, 0, 0, 0, 0, 1));
                        }
                    }
                }
            }
        }

        //Bepaald of het een wit of zwart paard is.
        if ((side == white) ? piece == N : piece == n)
        {
            //Zolang er een paard op het paard bitboard (kopie) is.
            while (bitboard)
            {
                //Bepaal waar de eerste knight staat van linksboven naar rechtsboven.
                source_square = get_ls1b_index(bitboard);
                //Attacks van knight is waar een knight naartoe kan bewegen, hierbij wordt rekening gehouden met zetten waarbij de knight op een stuk van zijn eigen kleur komt.
                attacks = knight_attacks[source_square] & ~occupancies[side];

                //Zolang er attack bits (of target_squares) zijn.
                while (attacks)
                {
                    //Krijg de eerste target square.
                    target_square = get_ls1b_index(attacks);
                    //Als aangevallen bit bezet is door tegenstanders kleur, dan de if, anders de else.
                    if (!get_bit(((side == white) ? occupancies[black] : occupancies[white]), target_square))
                    {
                        //Voeg de zet toe aan de mogelijke zetten.
                        add_move(move_list, encode_move(source_square, target_square, piece, 0, 0, 0, 0, 0));
                    }
                    //Voeg de zet toe aan de mogelijke zetten.
                    else add_move(move_list, encode_move(source_square, target_square, piece, 0, 1, 0, 0, 0));
                    //Verwijderd de attack zodat die naar de volgende attack gaat.
                    pop_bit(attacks, target_square);
                }
                //Verwijder bit van knight zodat die naar volgende knight gaat.
                pop_bit(bitboard, source_square);
            }
        }
        //Bepaald of het een witte of zwarte loper is.
        if ((side == white) ? piece == B : piece == b)
        {
            //Zolang er een loper op het loper bitboard (kopie) is.
            while (bitboard)
            {
                //Bepaal waar de eerste loper staat van linksboven naar rechtsonder.
                source_square = get_ls1b_index(bitboard);
                //Attacks van loper is waar een loper naartoe kan bewegen, hierbij wordt rekening gehouden met zetten waarbij de loper op een stuk van zijn eigen kleur komt.
                attacks = get_bishop_attacks(source_square, occupancies[both]) & ~occupancies[side];

                //Zolang er attacks bits (of target_squares) zijn.
                while (attacks)
                {
                    //Krijg de eerste target square.
                    target_square = get_ls1b_index(attacks);
                    //Als aangevallen bit bezet is door tegenstanders kleur, dan de if, anders de else.
                    if (!get_bit(((side == white) ? occupancies[black] : occupancies[white]), target_square))
                    {
                        //Voeg de zet toe aan de mogelijke zetten.
                        add_move(move_list, encode_move(source_square, target_square, piece, 0, 0, 0, 0, 0));
                    }
                    //Voeg de zet toe aan de mogelijke zetten.
                    else add_move(move_list, encode_move(source_square, target_square, piece, 0, 1, 0, 0, 0));
                    //Verwijderd de attack zodat die naar de volgende attack gaat.
                    pop_bit(attacks, target_square);
                }
                //Verwijder bit van loper bitboard zodat die naar volgende loper gaat.
                pop_bit(bitboard, source_square);
            }
        }
        //Bepaald of het een witte of zwarte toren is.
        if ((side == white) ? piece == R : piece == r)
        {
            //Zolang er een toren op het toren bitboard (kopie) is.
            while (bitboard)
            {
                //Bepaal waar de eerste toren staat van linksboven naar rechtsonder.
                source_square = get_ls1b_index(bitboard);
                //Attacks van loper is waar een loper naartoe kan bewegen, hierbij wordt rekening gehouden met zetten waarbij de loper op een stuk van zijn eigen kleur komt.
                attacks = get_rook_attacks(source_square, occupancies[both]) & ~occupancies[side];

                //Zolang er attacks bits (of target_squares) zijn.
                while (attacks)
                {
                    //Krijg de eerste target square.
                    target_square = get_ls1b_index(attacks);
                    //Als aangevallen bit bezet is door tegenstanders kleur, dan de if, anders de else.
                    if (!get_bit(((side == white) ? occupancies[black] : occupancies[white]), target_square))
                    {
                        //Voeg de zet toe aan de mogelijke zetten.
                        add_move(move_list, encode_move(source_square, target_square, piece, 0, 0, 0, 0, 0));
                    }
                    //Voeg de zet toe aan de mogelijke zetten.
                    else add_move(move_list, encode_move(source_square, target_square, piece, 0, 1, 0, 0, 0));
                    //Verwijder de attack zodat die naar de volgende attack gaat.
                    pop_bit(attacks, target_square);
                }
                //Verwijder bit van loper bitboard zodat die naar volgende loper gaat.
                pop_bit(bitboard, source_square);
            }
        }
        //Bepaal of het een witte of zwarte koningin is.
        if ((side == white) ? piece == Q : piece == q)
        {
            //Zolagn er een koningin op het koningin bitboard (kopie) is.
            while (bitboard)
            {
                //Bepaal waar de eerste toren staat van linksboven naar rechtsonder.
                source_square = get_ls1b_index(bitboard);
                //Attacks van loper is waar een loper naartoe kan bewegen, hierbij wordt rekening gehoduen met zetten waarbij de loper op een stuk van zijn eigen kleur komt.
                attacks = get_queen_attacks(source_square, occupancies[both]) & ~occupancies[side];

                //Zolang er attacks bits (of target_squares) zijn.
                while (attacks)
                {
                    //Krijg de eerste target square.
                    target_square = get_ls1b_index(attacks);
                    //Als aangevallen bit bezet is door tegenstanders kleur, dan de if, anders de else.
                    if (!get_bit(((side == white) ? occupancies[black] : occupancies[white]), target_square))
                    {
                        //Voeg de zet toe aan de mogelijke zetten.
                        add_move(move_list, encode_move(source_square, target_square, piece, 0, 0, 0, 0, 0));
                    }
                    //Voeg de zet toe aan de mogelijke zetten.
                    else add_move(move_list, encode_move(source_square, target_square, piece, 0, 1, 0, 0, 0));
                    //Verwijder de attack zodat die naar de volgende attack gaat.
                    pop_bit(attacks, target_square);
                }
                //Verwijder bit van koningin zodat die naar de volgende koningin gaat.
                pop_bit(bitboard, source_square);
            }
        }
        //Bepaal of het een witte of zwarte koning is.
        if ((side == white) ? piece == K : piece == k)
        {
            //Zolang er een koning op het koning bitboard (kopie) is.
            while (bitboard)
            {
                //Bepaal waar de eerste toren staat van linksboven naar rechtsonder.
                source_square = get_ls1b_index(bitboard);
                //Attacks van koning is waar een koning naartoe kan bewegen, hierbij wordt rekening gehoduen met zetten waarbij de koning op een stuk van zijn eigen kleur komt.
                attacks = king_attacks[source_square] & ~occupancies[side];

                //Zolang er attacks bits (of target_squares) zijn.
                while (attacks)
                {
                    //Krijg de eerste target square.
                    target_square = get_ls1b_index(attacks);
                    //Als aangevallen bit bezet is door tegenstanders kleur, dan de if, anders de else.
                    if (!get_bit(((side == white) ? occupancies[black] : occupancies[white]), target_square))
                    {
                        //Voeg de zet toe aan de mogelijke zetten.
                        add_move(move_list, encode_move(source_square, target_square, piece, 0, 0, 0, 0, 0));
                    }
                    //Voeg de zet toe aan de mogelijke zetten.
                    else add_move(move_list, encode_move(source_square, target_square, piece, 0, 1, 0, 0, 0));
                    //Verwijder de atttack zodat die naar de volgende attack gaat.
                    pop_bit(attacks, target_square);
                }
                //Verwijder bit van koning zodat die naar de volgende koning gaat.
                pop_bit(bitboard, source_square);
            }
        }
    }
}


/**************

EVALUATION

***************/
//Materiaal waarde scores.
int material_score[12] = {
    100,      // white pawn score
    300,      // white knight scrore
    350,      // white bishop score
    500,      // white rook score
   1000,      // white queen score
  10000,      // white king score
   -100,      // black pawn score
   -300,      // black knight scrore
   -350,      // black bishop score
   -500,      // black rook score
  -1000,      // black queen score
 -10000,      // black king score
};

// pawn positional score
const int pawn_score[64] =
{
    90,  90,  90,  90,  90,  90,  90,  90,
    30,  30,  30,  40,  40,  30,  30,  30,
    20,  20,  20,  30,  30,  30,  20,  20,
    10,  10,  10,  20,  20,  10,  10,  10,
     5,   5,  10,  20,  20,   5,   5,   5,
     0,   0,   0,   5,   5,   0,   0,   0,
     0,   0,   0, -10, -10,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0
};

// knight positional score
const int knight_score[64] =
{
    -5,   0,   0,   0,   0,   0,   0,  -5,
    -5,   0,   0,  10,  10,   0,   0,  -5,
    -5,   5,  20,  20,  20,  20,   5,  -5,
    -5,  10,  20,  30,  30,  20,  10,  -5,
    -5,  10,  20,  30,  30,  20,  10,  -5,
    -5,   5,  20,  10,  10,  20,   5,  -5,
    -5,   0,   0,   0,   0,   0,   0,  -5,
    -5, -10,   0,   0,   0,   0, -10,  -5
};

// bishop positional score
const int bishop_score[64] =
{
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,  10,  10,   0,   0,   0,
     0,   0,  10,  20,  20,  10,   0,   0,
     0,   0,  10,  20,  20,  10,   0,   0,
     0,  10,   0,   0,   0,   0,  10,   0,
     0,  30,   0,   0,   0,   0,  30,   0,
     0,   0, -10,   0,   0, -10,   0,   0

};

// rook positional score
const int rook_score[64] =
{
    50,  50,  50,  50,  50,  50,  50,  50,
    50,  50,  50,  50,  50,  50,  50,  50,
     0,   0,  10,  20,  20,  10,   0,   0,
     0,   0,  10,  20,  20,  10,   0,   0,
     0,   0,  10,  20,  20,  10,   0,   0,
     0,   0,  10,  20,  20,  10,   0,   0,
     0,   0,  10,  20,  20,  10,   0,   0,
     0,   0,   0,  20,  20,   0,   0,   0

};

// king positional score
const int king_score[64] =
{
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   5,   5,   5,   5,   0,   0,
     0,   5,   5,  10,  10,   5,   5,   0,
     0,   5,  10,  20,  20,  10,   5,   0,
     0,   5,  10,  20,  20,  10,   5,   0,
     0,   0,   5,  10,  10,   5,   0,   0,
     0,   5,   5,  -5,  -5,   0,   5,   0,
     0,   0,   5,   0, -15,   0,  10,   0
};

//Vakje van engines kant naar tegenstanders kant en andersom.
const int mirror_score[128] =
{
    a1, b1, c1, d1, e1, f1, g1, h1,
    a2, b2, c2, d2, e2, f2, g2, h2,
    a3, b3, c3, d3, e3, f3, g3, h3,
    a4, b4, c4, d4, e4, f4, g4, h4,
    a5, b5, c5, d5, e5, f5, g5, h5,
    a6, b6, c6, d6, e6, f6, g6, h6,
    a7, b7, c7, d7, e7, f7, g7, h7,
    a8, b8, c8, d8, e8, f8, g8, h8
};
//position evaluation
static inline int evaluate()
{
    //static evaluation score
    int score = 0;
    //current pieces bitboard copy
    U64 bitboard;
    // init piece & square
    int piece, square;

    // loop over piece bitboards
    for (int bb_piece = P; bb_piece <= k; bb_piece++)
    {
        // init bitboard piece copy
        bitboard = bitboards[bb_piece];
        //loop over pieces within a bitboard
        while (bitboard)
        {
            //init piece
            piece = bb_piece;
            //vakje moet gelijk zijn aan de ls1b index van het bitboard
            square = get_ls1b_index(bitboard);
            // score van de stukken is gelijk aan de material score
            score += material_score[piece];

            //Kies stuk.
            switch (piece)
            {
                //Tel de positional score van wit per stuk erbij op.
            case P: score += pawn_score[square]; break;
            case N: score += knight_score[square]; break;
            case B: score += bishop_score[square]; break;
            case R: score += rook_score[square]; break;
                //case Q: score += queen_score[square]; break;
            case K: score += king_score[square]; break;

                //Trek de positional score van wit per stuk ervanaf.
            case p: score -= pawn_score[mirror_score[square]]; break;
            case n: score -= knight_score[mirror_score[square]]; break;
            case b: score -= bishop_score[mirror_score[square]]; break;
            case r: score -= rook_score[mirror_score[square]]; break;
                //case q: score -= queen_score[mirror_score[square]]; break;
            case k: score -= king_score[mirror_score[square]]; break;
            }
            //Pop ls1b van kopie.
            pop_bit(bitboard, square);
        }
    }
    //Return final evaluation based on side (dus negatief voor zwart, positief voor wit).
    return (side == white) ? score : -score;
}
//Most valuable victim - Least valuable attacker tabel.
static int mvv_lva[12][12] = {
    105, 205, 305, 405, 505, 605,  105, 205, 305, 405, 505, 605,
    104, 204, 304, 404, 504, 604,  104, 204, 304, 404, 504, 604,
    103, 203, 303, 403, 503, 603,  103, 203, 303, 403, 503, 603,
    102, 202, 302, 402, 502, 602,  102, 202, 302, 402, 502, 602,
    101, 201, 301, 401, 501, 601,  101, 201, 301, 401, 501, 601,
    100, 200, 300, 400, 500, 600,  100, 200, 300, 400, 500, 600,

    105, 205, 305, 405, 505, 605,  105, 205, 305, 405, 505, 605,
    104, 204, 304, 404, 504, 604,  104, 204, 304, 404, 504, 604,
    103, 203, 303, 403, 503, 603,  103, 203, 303, 403, 503, 603,
    102, 202, 302, 402, 502, 602,  102, 202, 302, 402, 502, 602,
    101, 201, 301, 401, 501, 601,  101, 201, 301, 401, 501, 601,
    100, 200, 300, 400, 500, 600,  100, 200, 300, 400, 500, 600
};
// constante voor de max PLY in één search
const int MAX_PLY = 64;
// killer moves [id][ply]
int killer_moves[2][64];

// history moves [id][ply]
int history_moves[12][64];

int pv_length[64];
int pv_table[64][64];

int follow_pv, score_pv;

//Leaf nodes, aantal posities die behaald zijn tijdens een test met de move generator bij een bepaalde depth.
long nodes;

//Half move dus een zet van een kant.
int ply;

/**************

Transposition table

***************/
//hash table size
#define hash_size 0x400000

//no hash entry found constant
#define no_hash_entry 100000

//transposition table hash flags
#define   hash_flag_exact   0
#define    hash_flag_alpha   1
#define    hash_flag_beta    2


//transposition table structure
typedef struct  {
    U64 hash_key;
    int depth;
    int flag; //flag type of node (alpha cutoff/beta cutoff/PV)
    int score; //(alpha/beta/PC)
}   tt; //transposition table

// define TT instance
tt hash_table[hash_size];

//clear hash table
void clear_hash_table()
{
     //loop over tt elements
     for (int index = 0; index < hash_size; index++)
     {
        //reset TT inner fields
        hash_table[index].hash_key = 0;
        hash_table[index].depth = 0;
        hash_table[index].flag = 0;
        hash_table[index].score = 0;
     }
}

//Lees de hash entry data
static inline int read_hash_entry(int alpha, int beta, int depth)
{
    //Create a TT instance pointer naar de entry voor het opslaan
    tt *hash_entry = &hash_table[hash_key % hash_size];

    //Zeker weten dat de precieze positie wordt gepakt
    if (hash_entry->hash_key == hash_key)
    {
        //Zelfde depth
        if (hash_entry->depth >= depth)
        {
            //Match pv node score
            if (hash_entry->flag == hash_flag_exact)
                //Return exact pv node score
                {printf("exact score: "); return hash_entry->score;}

            //Match alpha, fail low node score
            if ((hash_entry->flag == hash_flag_alpha) && (hash_entry->score <= alpha))
                //Return exact pv node score
                {printf("alpha score: "); return alpha;}

            //Match beta, fail high node score
            if ((hash_entry->flag == hash_flag_beta) && (hash_entry->score >= beta))
                //Return exact pv node score
                {printf(" beta score: "); return beta;}
        }
    }
    //Als de hash->entry niet bestaat
    return no_hash_entry;
}

//Schrijf de hash entry data
static inline void write_hash_entry(int score, int depth, int hash_flag)
{
    //Create a TT instance pointer naar de entry voor het opslaan
    tt *hash_entry = &hash_table[hash_key % hash_size];

    //Schrijf de hash entry data
    hash_entry->hash_key = hash_key;
    hash_entry->score = score;
    hash_entry->flag = hash_flag;
    hash_entry->depth = depth;
}

// enable PV scoring
static inline void enable_pv_scoring(moves* move_list)
{
    // als we het gaan scoren, moeten we follow pv uitzetten
    follow_pv = 0;
    // over de moves in de move lijst loopen
    for (int count = 0; count < move_list->count; count++)
    {
        // hebben we de pv move
        if (pv_table[0][ply] == move_list->moves[count])
        {
            // enable move scoring
            score_pv = 1;
            // pv weer aanzetten
            follow_pv = 1;
        }
    }
}

//Geef elke zet een score kwa goed/slecht.
static inline int score_move(int move)
{
    int piece_score = material_score[get_move_piece(move)];


    // als pv move scoring aan staat
    if (score_pv)
    {
        // checken of het de pv move is
        if (pv_table[0][ply] == move)
        {
            // disable score PV flag
            score_pv = 0;
            // geef pv de hoogste score, zodat het als eerste move opkomt
            return 20000;
        }
    }

    //Capture moves.
    if (get_move_capture(move))
    {
        //Target_piece = witte pion (startwaarde vanwege ennpassant, anders crashed die).
        int target_piece = P;
        //Start_piece en end_piece oproepen.
        int start_piece, end_piece;

        //Als wit, dan loop zwarte stukken, als zwart dan loop witte stukken.
        if (side == white) { start_piece = p; end_piece = k; }
        else { start_piece = P; end_piece = K; }

        //Loop over bitboards van tegenstander.
        for (int bb_piece = start_piece; bb_piece <= end_piece; bb_piece++)
        {
            //Zoek het stuk van de tegenstander dat op het gegeven vakje staat.
            if (get_bit(bitboards[bb_piece], get_move_target(move)))
            {
                //Target_piece = aangevallen stuk;
                target_piece = bb_piece;
                break;
            }
        }

        //Return score [source piece][target piece]. 10000 staat niet in video 54 (dit staat in 57).
        return mvv_lva[get_move_piece(move)][target_piece] + 10000;
    }

    // Score quiet move.
    else
    {
        //score eerste killer move.
        if (killer_moves[0][ply] == move)
            return 9000;
        //score tweede killer move.
        else if (killer_moves[1][ply] == move)
            return 8000;
        else
            //score history moves
            return history_moves[get_move_piece(move)][get_move_target(move)] + piece_score;
    }
    return 0;
}

//Sort moves op basis van score (hoog naar laag).
static inline int sort_moves(moves* move_list)
{
    //Move scores array. Deze array is even groot als het aantal moves in move_list.
    int move_scores[move_list->count];

    //Loop over alle moves in move_list.
    for (int count = 0; count < move_list->count; count++)
        //Score alle moves.
        move_scores[count] = score_move(move_list->moves[count]);

    //Loop over current move in move_list.
    for (int current = 0; current < move_list->count; current++)
    {
        //Loop over volgende move in move_list.
        for (int next = current + 1; next < move_list->count; next++)
        {
            //Als current move_score is kleiner dan volgende move_score. Dan switch de zetten.
            if (move_scores[current] < move_scores[next])
            {
                //Store current score.
                int temp_score = move_scores[current];
                //Move_score wordt move_score van volgende.
                move_scores[current] = move_scores[next];
                //Move score van volgende wordt temp_score wat de move score van current is.
                move_scores[next] = temp_score;

                //Precies hetzelfde als de score hierboven maar dan met de move ipv score.
                int temp_move = move_list->moves[current];
                move_list->moves[current] = move_list->moves[next];
                move_list->moves[next] = temp_move;
            }
        }
    }
    return 0;
}

//Print move scores.
void print_move_scores(moves* move_list)
{
    //Print "Move scores:".
    printf("     Move scores:\n\n");

    //Loop over moves in move_list.
    for (int count = 0; count < move_list->count; count++)
    {
        //Print zet en score zoals "move: e2e4 score: 0".
        printf("     move: ");
        print_move(move_list->moves[count]);
        printf(" score: %d\n", score_move(move_list->moves[count]));
    }
}
// quiescence search
static inline int quiescence(int alpha, int beta)
{
    // elke 2047 nodes.
    if ((nodes & 2047) == 0)
        //krijg gui input
        communicate();

    // verhoog node count
    nodes++;

    // evaluatie functie
    int eval = evaluate();

    if (eval >= beta)
    {
        return beta;
    }

    if (eval > alpha)
    {
        alpha = eval;
    }

    moves move_list[1];

    //generate moves
    generate_moves(move_list);

    sort_moves(move_list);

    //loop over moves within a movelist
    for (int count = 0; count < move_list->count; count++)
    {
        //preserve board state
        copy_board();
        //verhoog ply
        ply++;

        //alleen legale moves
        if (make_move(move_list->moves[count], only_captures) == 0)
        {
            //Ply -1.
            ply--;
            //Ga naar volgende zet.
            continue;
        }
        //score current move
        int score = -quiescence(-beta, -alpha);
        //verlaag ply
        ply--;
        //take move back
        take_back();

        //return 0 als de tijd op is
        if (stopped == 1) return 0;

        //fail hard beta cutoff
        if (score >= beta)
        {
            //node (move) fails high
            return beta;
        }

        //found a better move
        if (score > alpha)
        {
            //PV node (move)
            alpha = score;
        }
    }
    //node (move) fails low (of hetzelfde dan alpha of lager)
    return alpha;
}
const int full_depth_moves = 4;
const int reduction_limit = 3;


//Negamax alpha-beta search.
static inline int negamax(int alpha, int beta, int depth)
{
    // de find pv variabele definiëren


    // init pv length
    pv_length[ply] = ply;

    //Als de depth 0 is, dan return iets??
    if (depth == 0)
        return quiescence(alpha, beta);
    // zorgt ervoor dat engine niet crash (ofwel niet overflowt)
    if (ply > MAX_PLY - 1)
        return evaluate();

    //Vergroot nodes.
    nodes++;

    //Variabele in_check (koning).
    int in_check = is_square_attacked((side == white) ? get_ls1b_index(bitboards[K]) :
        get_ls1b_index(bitboards[k]), side ^ 1);

    if (in_check) depth++;

    //Legal moves variabele.
    int legal_moves = 0;

    //null move pruning
    if (depth >= 3 && in_check == 0 && ply)
    {
        //preserve board state
        copy_board();

        //switching side, so opponent has an extra move to make
        side ^= 1;

        //reset enpassement capture square
        enpassant = no_sq;

        //search moves with reduced search depth to find beta cutoffs (R is dus een reduction limit)
        int score = -negamax(-beta, -beta + 1, depth - 1 - 2);

        //restore board state
        take_back();

        //fail hard beta cutoff
        if (score >= beta)
            return beta;
    }
    //Creeër move list genaamd moves.
    moves move_list[1];
    //Genereer zetten.
    generate_moves(move_list);

    // als we pv gebruiken, dan moet de pv move gescored worden
    if (follow_pv)
        // dus zet pv scoring aan
        enable_pv_scoring(move_list);

    sort_moves(move_list);

    //number of moves searched in a move list
    int moves_searched = 0;

    //Loop over zetten in move_list.
    for (int count = 0; count < move_list->count; count++)
    {
        //Maak een kopie van het board.
        copy_board();
        //Half move++
        ply++;

        //Als zet niet legaal is, dan ply - 1.
        if (make_move(move_list->moves[count], all_moves) == 0)
        {
            //Ply -1 en ga verder.
            ply--;
            continue;
        }
        //Legale zetten +1
        legal_moves++;

        //Variabele to score current move (van de static evaluation)
        int score;

        // Full depth searched
        if (moves_searched == 0) // First move, use full-window search
        //Geef de zet een score.
            score = -negamax( -beta, -alpha, depth - 1);

        // LMR
        else
        {
            // condition to consider LMR
            if (
                moves_searched >= full_depth_moves &&
                depth >= reduction_limit &&
                in_check == 0 &&
                get_move_capture(move_list->moves[count]) == 0 &&
                get_move_promoted(move_list->moves[count]) == 0
                )
                // Search this move with reduced depth:
                score = -negamax(-alpha - 1, -alpha, depth - 2);

                // Hack to ensure that full-depth search is done.
            else score = alpha + 1;

            // PVS
            if (score > alpha)
            {
                //Wanneer je een score hebt gevonden tussen alpha en beta, de rest van de moves wordt dan vanuit gegaan dat ze slecht zijn. Dit is sneller dan als gedacht wordt dat er wel nog een goede move is.
                score = -negamax(-alpha - 1, -alpha, depth - 1);

                // Als er toch een betere move is, dan moet op de normale alpha beta manier gezocht worden.
                if ((score > alpha) && (score < beta))
                    score = -negamax(-beta, -alpha, depth - 1);
            }
        }



        //Ply -1.
        ply--;

        //Zet terug nemen.
        take_back();

        //return 0 als de tijd op is
        if (stopped == 1) return 0;

        //increment moves searched
        moves_searched++;

        //Als de score groter of gelijk is aan beta. (Node fails high).
        if (score >= beta)
        {
            // voor quiet moves
            if (get_move_capture(move_list->moves[count]) == 0)
            {
                //store killer moves
                killer_moves[1][ply] = killer_moves[0][ply];
                killer_moves[0][ply] = move_list->moves[count];
            }
            //Return beta.
            return beta;
        }

        //Als de score groter is dan alpha (betere zet).
        if (score > alpha)
        {
            // voor quiet moves
            if (get_move_capture(move_list->moves[count]) == 0)
                //store history moves
                history_moves[get_move_piece(move_list->moves[count])][get_move_target(move_list->moves[count])] += depth;

            //Stel alpha gelijk aan score.
            alpha = score;

            // schrijf een pv move uit de tabel
            pv_table[ply][ply] = move_list->moves[count];
            // loop over de ply
            for (int next = ply + 1; next < pv_length[ply + 1]; next++)
                // De volgende moves die nog moeten komen uit de tabel moeten in de lijn van deze ply worden gezet
                pv_table[ply][next] = pv_table[ply + 1][next];

            // verander pv length
            pv_length[ply] = pv_length[ply + 1];
        }
    }
    //Als er geen legale zetten zijn.
    if (legal_moves == 0)
    {
        //En als de koning in check is.
        if (in_check)
            //Dan return mate. +ply want mate in 1 gaat voor mate in 10.
            return -49000 + ply;
        else
            //Anders stalemate.
            return 0;
    }
    //Return alpha. (Node fails low).
    return alpha;
}

//Search positie voor beste zet.
void search_position(int depth)
{
    int score = 0;
    //reset nodes teller voor nieuwe evaluation
    nodes = 0;
    //reset time is op flag
    stopped = 0;
    //reset pv flags
    follow_pv = 0;
    score_pv = 0;
    // clear "cache" van gebruikte tabellen van de killer moves/history moves/ pv table/ pv length
    memset(killer_moves, 0, sizeof(killer_moves));
    memset(history_moves, 0, sizeof(history_moves));
    memset(pv_table, 0, sizeof(pv_table));
    memset(pv_length, 0, sizeof(pv_length));

    //Define alpha beta values
    int alpha = -50000;
    int beta = 50000;

    // iterative deepening (evaluation maar dan per depth)
    for (int current = 1; current <= depth; current++)
    {
        //als de tijd op is
        if (stopped == 1)
            //stop calculating zetten en geef de beste zet
            break;

        // we volgen pv, dus we zetten pv aan
        follow_pv = 1;

        //Zoek de beste zet in de gegeven positie.
        int score = negamax(alpha, beta, current);

        if ((score <= alpha) || (score >= beta)) {
            alpha = -50000;    // We fell outside the window, so try again with a
            beta = 50000;      //  full-width window (and the same depth).
            continue;

        }
        // Set up the window for the next iteration.
        alpha = score - 50;
        beta = score + 50;


        //Print belangrijke info.
        printf("info score cp %d depth %d nodes %ld time %d pv ", score, current, nodes, get_time_ms() - starttime);

        for (int count = 0; count < pv_length[0]; count++)
        {
            print_move(pv_table[0][count]);
            printf(" ");
        }
        printf("\n");
    }
    //Print best move.
    printf("bestmove ");
    print_move(pv_table[0][0]);
    printf("\n");
}


/**************

UCI

***************/
//Geef de user/GUI move string input (bv e7e8q). (binnen de haakjes is anders in vid 44)
int parse_move(char *move_string)
    {
    //Generate move list.
    moves move_list[1];
    //Generate moves.
    generate_moves(move_list);

    //Eerste 2 letters/cijfers van move_string (source square).
    int source_square = (move_string[0] - 'a') + (8 - (move_string[1] - '0')) * 8;
    //Derde en vierde letters/cijfers van move_string (target square).
    int target_square = (move_string[2] - 'a') + (8 - (move_string[3] - '0')) * 8;

    //Loop moves in move list.
    for (int move_count = 0; move_count < move_list->count; move_count++)
    {
        //Init move.
        int move = move_list->moves[move_count];
        //Zeker weten dat de source_square en target_square mogelijk zijn.
        if (source_square == get_move_source(move) && target_square == get_move_target(move))
        {
            //Init promoted piece.
            int promoted_piece = get_move_promoted(move);

            //Als promotie in
            if (promoted_piece)
            {
                //Als 5e cijfer/letter een qrb of n, dan promotie.
                if (move_string[4] == 'q' || move_string[4] == 'r' || move_string[4] == 'b' || move_string[4] == 'n')
                    //Return legal move.
                    return move;
                //Return illegal move.
                return 0;
            }
            //Return legal move. (in tutorial "return 1" vid 44)
            return move;
        }
    }
    //Return illegal move.
    return 0;
}

//Parce uci position commmand.
void parse_position(const char* command)
{
    //Ga 9 tekens verder in de command. (bv "postion startpos" leest die vanaf startpos want dat is belangrijk en daarvoor niet).
    command += 9;
    //Current_char = command.
    const char* current_char = command;

    //Als de letters na postition startpos zijn dus command "position startpos".
    if (strncmp(command, "startpos", 8) == 0)
        //Dan fen = start_position.
        parse_fen(start_position);
    //Anders.
    else
    {

        //Current_char = fen command is in string.
        current_char = strstr(command, "fen");

        //Als er geen fen is in current_char.
        if (current_char == NULL)
            //Fen = startposition
            parse_fen(start_position);
        //Anders (wel een fen).
        else
        {
            //Ga 4 verder (skip fen en de spatie).
            current_char += 4;
            //Parse_fen wat over is (de fen).
            parse_fen(current_char);
        }
    }
    //Parse moves na de fen.
    current_char = strstr(command, "moves");

    //Als er nog zetten zijn.
    if (current_char != NULL)
    {
        //Ga 6 verder, moves en een spatie.
        current_char += 6;
        //Loop over tekens in de overige string.
        while (*current_char)
        {
            //Een legale zet of 0 als illegaal.
            int move = parse_move(current_char);
            //Als er niet meer zetten zijn.
            if (move == 0)
                //Stop de loop.
                break;

            //Doe de zetten.
            make_move(move, all_moves);
            //Zolang er geen spatie is, naar volgende teken gaan.
            while (*current_char && *current_char != ' ') current_char++;
            //Ga naar volgende zet.
            current_char++;
        }
    }
}

//UCI go command.
void parse_go(const char* command)
{
    // init parameters
    int depth = -1;

    // init argument
    char* argument = NULL;

    // infinite search
    if ((argument = strstr(command, "infinite"))) {}

    // match UCI "binc" command
    if ((argument = strstr(command, "binc")) && side == black)
        // parse black time increment
        inc = atoi(argument + 5);

    // match UCI "winc" command
    if ((argument = strstr(command, "winc")) && side == white)
        // parse white time increment
        inc = atoi(argument + 5);

    // match UCI "wtime" command
    if ((argument = strstr(command, "wtime")) && side == white)
        // parse white time limit
        time = atoi(argument + 6);

    // match UCI "btime" command
    if ((argument = strstr(command, "btime")) && side == black)
        // parse black time limit
        time = atoi(argument + 6);

    // match UCI "movestogo" command
    if ((argument = strstr(command, "movestogo")))
        // parse number of moves to go
        movestogo = atoi(argument + 10);

    // match UCI "movetime" command
    if ((argument = strstr(command, "movetime")))
        // parse amount of time allowed to spend to make a move
        movetime = atoi(argument + 9);

    // match UCI "depth" command
    if ((argument = strstr(command, "depth")))
        // parse search depth
        depth = atoi(argument + 6);

    // if move time is not available
    if (movetime != -1)
    {
        // set time equal to move time
        time = movetime;

        // set moves to go to 1
        movestogo = 1;
    }

    // init start time
    starttime = get_time_ms();

    // init search depth
    depth = depth;

    // if time control is available
    if (time != -1)
    {
        // flag we're playing with time control
        timeset = 1;

        // set up timing
        time /= movestogo;
        time -= 50;
        stoptime = starttime + time + inc;
    }

    // if depth is not available
    if (depth == -1)
        // set depth to 64 plies (takes ages to complete...)
        depth = 64;

    // print debug info
    printf("time:%d start:%d stop:%d depth:%d timeset:%d\n",
    time, starttime, stoptime, depth, timeset);

    //idk???
    search_position(depth);
}

//Belangrijkste UCI loop
void uci_loop()
{
    //Reset stdin en stdout buffers.
    setbuf(stdin, NULL);
    setbuf(stdout, NULL);

    //Groot nummer want moet altijd genoeg tekens zijn in de input, kan best veel zijn.
    char input[2000];

    //Print engine info
    printf("id name AlphaEngine\n");
    printf("id author Mart:)\n");
    printf("uciok\n");

    //Main loop (1 staat voor true)
    while (1)
    {
        //Reset user /gui input.
        memset(input, 0, sizeof(input));
        //Zorgt ervoor dat de output bij de gui aankomt.
        fflush(stdout);

        //Get user / GUI input.
        if (!fgets(input, 2000, stdin))
            continue;
        //Anders check dat het niet begint met een nieuwe regel.
        if (input[0] == '\n')
            continue;
        //Anders als isready, dan print readyok.
        if (strncmp(input, "isready", 7) == 0)
        {
            //Print ready ok met een nieuwe regel.
            printf("readyok\n");
            continue;
        }
        //Anders als position, dan parse position.
        else if (strncmp(input, "position", 8) == 0)
            parse_position(input);
        //Anders als ucinewgame, dan parse start_position.
        else if (strncmp(input, "ucinewgame", 10) == 0)
            parse_position("position startpos");
        //Anders als go, dan parse go.
        else if (strncmp(input, "go", 2) == 0)
            parse_go(input);
        //Anders als quit, dan break (stop).
        else if (strncmp(input, "quit", 4) == 0)
            break;
        //Anders als uci, dan info printen.
        else if (strncmp(input, "uci", 3) == 0)
        {
            printf("id name AlphaEngine\n");
            printf("id author Mart\n");
            printf("uciok\n");
        }
    }
}



/**************

MAIN DRIVER

***************/
//Initiate alle rook en bishop attacks.
void init_all()
{
    //Initiate de verschillende aanvallen
    init_leapers_attacks();
    init_sliders_attacks(bishop);
    init_sliders_attacks(rook);
    //init random keys voor het hashen
    init_random_keys;
}


// leaf nodes (number of positions reached during the test of the move generator at a given depth)
long nodes;

//Perft driver.
static inline void perft_driver(int depth)
{
    //Als de depth 0 is.
    if (depth == 0)
    {
        //Tel 1 bij nodes op.
        nodes++;
        return;
    }

    //Generate move list
    moves move_list[1];
    //Generate zetten in de move list.
    generate_moves(move_list);

    //Loop over alle gegenereerde zetten in move_list.
    for (int move_count = 0; move_count < move_list->count; move_count++)
    {
        //Onthoud eerdere bordstatus.
        copy_board();

        //Maak zet???
        if (!make_move(move_list->moves[move_count], all_moves)) continue;
        //Depth 1 naar beneden want net zet gedaan en zometeen zet terug dus depth blijft hetzelfde.
        perft_driver(depth - 1);

        //Take back.
        take_back();


        // ===== debug hash key incremental update ===== //

        //Maak een hask key voor updated positie
        U64 hash_from_scratch = generate_hash_key();

        //Als de hash key niet overeenkomt met een incrementaly updated hask key, dan stop even.
        if (hash_key != hash_from_scratch)
        {
            //voor debuggen
            printf("\n\nTake Back\n");
            printf("move: "); print_move(move_list->moves[move_count]);
            print_board();
            printf("hash key should be: %llx\n", hash_from_scratch);
            getchar();
        }
    }
}
//Perft test (performance test zoals aantal nodes per depth).
void perft(int depth)
{
    //Print "performance test".
    printf("\n     Performance test\n\n");
    //Generate move list.
    moves move_list[1];
    //Generate zetten in de move list.
    generate_moves(move_list);
    //Start tracking tijd.
    long start = get_time_ms();
    //Loop over alle gegenereerde zetten in  move_list.
    for (int move_count = 0; move_count < move_list->count; move_count++)
    {
        //Onthoud eerdere bordstatus.
        copy_board();
        //Maak zet???
        if (!make_move(move_list->moves[move_count], all_moves)) continue;
        //Alle nodes samen.
        long all_nodes = nodes;
        perft_driver(depth - 1);
        //Old nodes, de nodes per zet in depth 1.
        long old_nodes = nodes - all_nodes;
        //Take back.
        take_back();
        //Print zet met aantal nodes erachter.
        printf("     move: %s%s%c  nodes: %ld\n", square_to_coordinates[get_move_source(move_list->moves[move_count])],
            square_to_coordinates[get_move_target(move_list->moves[move_count])],
            get_move_promoted(move_list->moves[move_count]) ? promoted_pieces[get_move_promoted(move_list->moves[move_count])] : ' ',
            old_nodes);
    }
    //Print depth.
    printf("\n\n    Depth:   %d\n", depth);
    //Print aantal nodes.
    printf("    Nodes:   %ld\n", nodes);
    //Print tijd sinds laatste zet in ms.
    printf("    Time:    %ld\n", get_time_ms() - start);
}

int main()
{
    init_all();

    int debug = 1;
    // if debugging
    if (debug)
    {
        search_position(7);
        // parse fen
        /*parse_fen(start_position);
        print_board();
        //search_position(7);

        // clear hash table
        clear_hash_table();

        // write example entry to hash table
        write_hash_entry(21, 1, hash_flag_beta);

        // read score from hash table
        int score = read_hash_entry(20, 30, 1);

        // print score from hash entry
        printf("%d\n", score);*/
    }

    //als perft
    int performance_test = 0;
    //if perf test
    if (performance_test)
    {
        parse_fen(start_position);
        print_board();
        for (int i = 1; i < 7; i++) {
            perft(i);

        }
    }
    else uci_loop();

    return 0;
}
