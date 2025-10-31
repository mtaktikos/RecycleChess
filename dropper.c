#define VERSION "1.0.5"

/********************************************************************************************/
/* Simple XBoard-compatible engine for playing Chess variants with drops, by H.G. Muller.   */
/* Handles boards up to 11 x 11, with up to 16 droppable piece types, and 15 promoted types.*/
/********************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG 0
#define IDQS /* Iteratively deepening QS */
#define LMR 2

#define ON  1
#define OFF 0
#define INVALID 0

#define INF    15000
#define MAXPLY   100
#define MAXMOVES 500

#define NONE    0
#define ANALYZE 1
#define WHITE  32
#define BLACK  64
#define COLOR (WHITE | BLACK)

#define CK_UNKNOWN 255
#define CK_NONE    254
#define CK_DOUBLE  253

#define C_DISTANT 0xFF00
#define C_CONTACT 0x00FF

typedef long long int Key;

int ply, nodeCount, forceMove, choice, rootMove, lastGameMove, rootScore, abortFlag, postThinking=1; // some frequently used data
int maxDepth=MAXPLY-2, timeControl=3000, mps=40, inc, timePerMove, timeLeft=1000; // TC parameters

#define H_LOWER 1
#define H_UPPER 2

int ReadClock (int start);
char *MoveToText (int move);
int TimeIsUp (int mode);

int randomize, ranKey;
int moveNr;              // part of game state; incremented by MakeMove

#define captCode (rawInts + 22*10 + 10)
#define deltaVec (rawChar + 22*10 + 10)
#define promoInc (rawChar + 21*22)
#define board    (rawByte + 2*22 + 2)
#define pawnCount (board + 9*22 + 11)
#define dropType (rawByte + 15*22)
#define toDecode (rawByte + 26*22)
#define spoiler  (rawByte + 37*22)
#define zoneTab  (rawByte + 48*22)
#define sqr2file (rawByte + 59*22)
#define dist     (rawByte + 70*22 + 22*10 + 10)
#define dropBulk (rawByte + 91*22)
#define location (rawLocation + 23)
#define promoGain (rawGain + 1)

int pvStack[MAXPLY*MAXPLY/2];
int nrRanks, nrFiles, specials, pinCodes, maxDrop, moveSP, pawn, queen, lanceMask, *pvPtr = pvStack, boardEnd, perpLoses, searchNr;
int  frontier, killZone, impasse, frontierPenalty, killPenalty;
int rawInts[21*22], pieceValues[96], pieceCode[96];
signed char   rawChar[32*22], steps[512];
unsigned char rawByte[102*22], firstDir[64], rawBulk[98], handSlot[97], handSlotSame[97], promoCode[96], aVal[64], vVal[64], rawLocation[96+23], handBulk[96];
long long int handKey[96], handKeySame[96], pawnKey;
int handVal[96], handValSame[96], rawGain[97];
unsigned int moveStack[500*MAXPLY];
int killers[MAXPLY][2];
int path[MAXPLY], deprec[MAXPLY];
int history[1<<16], mateKillers[1<<17];
int anaSP;
unsigned char checkHist[MAXMOVES+MAXPLY];
int repKey[512+100];
unsigned char repDep[512+100];

/*
   Drops: piece is the (negated) count, promo the piece to be dropped.
   The from-square has to be cleared for moves, but incremented for drops.
       piece = board[from];
       board[from] = (piece >> 7) & (piece + 1); // 0 if piece >= 0
   Promotion on moves indicated by to-square
       promo = piece + promoTab[to];
       to = toTab[to];
   On drops, however, it is determined by the from-square
       promo = dropTab[from];
   Combine:
       promo = dropTab[from] + promoTab[to] + (piece & ~mask);

   piece = board[from];              // off-board is negative
   mask  = piece >> 7;               // on board 0, off board -1
   board[from] = mask & (piece + 1); //
   promo = dropTab[from] + promoTab[to];


   dropTab[] = {
      0, 0, 0, 0, 0,     1, 2, 3, 4, 5,  // off-board from-square contains piece codes
      0, 0, 0, 0, 0,     6, 7, 8, 9,10,
      0, 0, 0, 0, 0,     0, 0, 0, 0, 0,
      0, 0, 0, 0, 0,     0, 0, 0, 0, 0,
      0, 0, 0, 0, 0,     0, 0, 0, 0, 0,
   };
   promoTab[] = {
      0, 0, 0, 0, 0,     8, 8, 8, 8, 8,
      0, 0, 0, 0, 0,     8, 8, 8, 8, 8,
      0, 0, 0, 0, 0,     8, 8, 8, 8, 8,
      0, 0, 0, 0, 0,     8, 8, 8, 8, 8,
      0, 0, 0, 0, 0,     8, 8, 8, 8, 8,
   };

   encoding:
   0 = empty
   1-16 = unpromoted pieces    SP SC SO FC FG CM BD VS VW OC LH SW FF RR TR CE
   17-31 = promoted pieces     GB FF CE RF SW VS VW RB BE PO HH GS TF TR  K
   promotion adds 16
   handTab[] = {               11 12 13 14 15 16 17 18 19 20 21 33 34 34 36 37

 1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16
                                1  2  3  4  5  6  7  8  9 10 11 12 13 14 15    };
                               P  N  B  R  Q
                                  N~ B~ R~ Q~                             K
   promotion adds 17-20
   32 = white
   64 = black
   128 = off board
   negative piece types = hand counts
   all of those point to same PST, which only has to exist for off-board squares
 */

// Capture codes
// the needs of shogi and chess can be met with 8 contact and 4 distant bits,
// by splitting the Knight and Rook in f, b and s components
// wa needs additional f/bW2 (LH) and f/bW3 (CE) distant, and AvD (TF) instead of sN
// Chess fF, bF, fW, bW, sW, fN, bN, sN (6) R, B (2)
//   wP   x
//   bP       x
//    N                       x   x   x
//    B   x   x                                x
//    R           x   x   x                 x
//    Q   x   x   x   x   x                 x  x
//    K   x   x   x   x   x
// Shogi fF, bF, fW, bW, sW, fN, bN (7) fR, bR, sR, B (5)
//   wP           x
//   wL                                  x
//   wN                       x
//   wS   x   x   x
//   wG   x       x   x   x
//    B   x   x                                      x
//    R           x   x   x              x   x   x
//   +B   x   x   x   x   x                          x
//   +R   x   x   x   x   x              x   x   x
//    K   x   x   x   x   x
// Tori  fFbD, bFfD, fW, bW, sW, F, fAbD, bAfD (8) fRblB, fRbrB, bRflB, bRfrB, fA, bA (6)
//   wP               x
//  wPh          x
//  wCr               x   x      x
//  wQl          x    x                              x
//  wQr          x    x                                     x
//  wFa               x       x  x
//  +wP                               x
// +wFa               x   x   x  x                                 x      x         x
//    K               x   x   x  x
// Wa    fF, bF, fW, bW, sW, vN, AvD (7) fR, bR, sR, B, fGfA, bGbA, fD, bD (8)
/*
   The slider tracks skip the first square.
   Test for slider alignment first. If hit, we do ray scan, and never try leaps.
   This allows slides to mask jumps, so that a Quail can have bFfD + fRlbB.
 */

// for each of the 16 bits in the capture codes there is a zero-terminated list of board steps to which it corresponds
// the first 8 are leaps, the next 4 unlimited-range sliders, than a pair of range 2 and a pair of range 3.
signed char
toriCodes[]  = { 21,23,-44,0, -21,-23,44,0, 22,0,-22,0, 1,-1,0, 21,23,-21,-23,0, 42,46,-44,0, -42,-46,44,0,
                22,-23,0, 22,-21,0, -22,23,0, -22,21,0, 21,23,0, -21,-23,0, 0,0 },
chessCodes[] = { 21,23,0, -21,-23,0, 22,0, -22,0, 1,-1,0, 43,45,0, -43,-45,0, 20,24,-20,-24,0,           22,0, -22,0, 1,-1,0, 21,23,-21,-23,0,0,0,0,0,0,0},
shogiCodes[] = { 21,23,0, -21,-23,0, 22,0, -22,0, 1,-1,0, 43,45,0, -43,-45,0, 42,44,46,-42,-44,-46,0,    22,0, -22,0, 1,-1,0, 21,23,-21,-23,0,
													 22,0, -22,0, 21,23,0, -21,-23,0 };

void
InitCaptureCodes(signed char *codes)
{
    int i, piece, dir=0;
    for(i=-10-10*22; i<=10+10*22; i++) captCode[i] = 0; // clear capture codes and step vectors
    // build variant-specific alignment map, marking each square with the capture sets to which it belongs
    for(i=0; i<16; i++) { // for all 16 capture sets
	int step, b = 1<<i, range = 10;    // unlimited range
	if(i >= 12) range = i/2 - 4; // 2 or 3, for sets 12/13 and 14/15, respectively
	while((step = codes[dir++])) {
	    if(i < 8) captCode[step] |= b; else { // first 8 capture sets are leaps
		int d; // other are slides, so scan ray
		for(d=2; d<=range; d++) captCode[d*step] |= b;
		for(d=1; d<=10; d++) deltaVec[d*step] = step, dist[d*step] = d; // step for sliding to square
	    }
	}
    }
    // collect codes of squares that each piece hits
    for(piece=WHITE; piece<BLACK+32; piece++) {
	int step, code = 0;
	dir = 2*firstDir[piece-WHITE];
	if(!dir) continue;
	while((step = steps[dir++])) {
	    int range = steps[dir++], c = captCode[step] & C_DISTANT;
	    if((range & 3) == 2) continue;
	    range = range + 7 >> 3;
	    if(c) code |= c; // jump excludes slides to same square (which would mask it)
	    for(i=1; i<=range; i++) captCode[i*step] ^= -1; // flip reachable codes, clear bits
	}
	for(i=-10-10*22; i<=10+10*22; i++) { // scan over all possible moves
	    int c = captCode[i];
	    if(c < 0) captCode[i] ^= -1; // restore reachable squares to normal
	    else code |= c; // set bits of sets that contain squares we cannot reach
	}
	pieceCode[piece] = code ^ 0xFFFF;
#if 0
	{
	    code = captCode[step] & C_CONTACT; // first step: unblockable codes
	    for(i=2; i<=range; i++) code |= captCode[i*step] & C_DISTANT; // later steps are blockable
	}
#endif
    }
}

/*
   Piece encoding
            0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
    Wa:    SP OC LH SC SO CM BD FC FG VS VW SW FF RR CE Tr
           GB PO HH FF CE VS VW RF SW RB BE GS TF Tr    CK
    Shogi:  P  N  B  R  L  S  R
           +P +N  H  D +L +S                             K
    Tori:   S Ph Cr Ql Qr Fa
            G             Ea                            Px
    Chess:  P  N  B  R  A  C  Q
           Q~ N~ B~ R~ A~ C~                             K
 */

unsigned char handSlot[97], rawBulk[98];

#define pawnBulk (rawBulk + 1)
#define maxBulk  (rawBulk + 1 + 30) /* piece type 30 is not even used in Wa */

// Move tables: (step,range) pair for each direction a piece moves in, terminated by (0,0) sentinel
//              divergent moves (for FIDE Pawn) are flagged in high nibble of range byte
//              *** Note that this is re-packed at run time to have flags in low three bits! ***
#define MOVE_ONLY  32
#define CAPT_ONLY (16+32)

signed char steps[] = {
    21,10, 23,10, -21,10, -23,10, 20,1, 24,1, -20,1, -24,1, 43,1, 45,1, -43,1, -45,1, 0,0, // A=0, N=4, HH=8, bN=10
    1,10, -1,10, 22,10, -22,10, 21,10, 23,10, -21,10, -23,10, 0,0, // Q=13
    21,1, -21,1, 23,1, -23,1, 1,10, -1,10, 22,10, -22,10, 0,0,     // +R=22, R=26
    1,1, -1,1, 22,1, -22,1, 21,10, 23,10, -21,10, -23,10, 0,0,     // +B=31, bFF=34, B=35
    1,1, -1,1, 22,1, -22,1, 21,1, 23,1, -21,1, -23,1, 0,0,         // K=40, FL=42, bS=43
    22,2+MOVE_ONLY,   21,1+CAPT_ONLY,  23,1+CAPT_ONLY, 0,0,        // wP=49
    -22,2+MOVE_ONLY, -21,1+CAPT_ONLY, -23,1+CAPT_ONLY, 0,0,        // bP=53
    1,1, -1,1, 22,1, 21,1, 23,1, -22,1, 0,0,         // wG=57, wC=59, bSO=60, bP=62
    1,1, -1,1, -22,1, -21,1, -23,1, 22,1, 0,0,       // bG=64, bC=66, wSO=67, wP=69
    -21,1, -23,1, -22,1, 1,1, -1,1, 21,1, 23,1, 0,0, // bDE=71, wBD=73, wFC=74
    21,1, 23,1, 22,1, 1,1, -1,1, -21,1, -23,1, 0,0,  // wDE=79, bBD=81, bFC=82 
    22,1, 21,1, 23,1, -21,1, -23,1, 0,0, // wS=87
    22,2, -22,10, 0,0,                   // bLH=93, bL=94
    -22,2, 22,10, 0,0,                   // wLH=96, wL=97
    22,1, -22,1, 21,1, 23,1, -21,1, -23,1, -44,1, 46,1, 42,1, -46,1, -42,1, 44,1, 0,0, // TF=99, +bSw=108
    46,1, 42,1, -44,1, 0,0,                      // +wSw=112
    22,1, -22,1, 1,10, -1,10, 0,0,               // SW=116
    22,10, -22,1, 21,1, 23,1, -21,1, -23,1, 0,0, // wRR=121
    -22,10, 22,1, -21,1, -23,1, 21,1, 23,1, 0,0, // bRR=128
    22,1, 21,10, 23,10, -21,10, -23,10, 0,0,     // wFF=135
    1,1, -1,1, 22,10, -22,10, 21,10, 23,10, -21,10, -23,10, 0,0, // TF=141
    1,1, -1,1, 22,10, -22,10, 21,3, 23,3, -21,1, -23,1, 0,0,     // wCE=150
    1,1, -1,1, 22,10, -22,10, -21,3, -23,3, 21,1, 23,1, 0,0,     // bCE=159
    1,1, -1,1, 22,1, -22,10, 21,10, 23,10, -21,2, -23,2, 0,0,    // +wFa=168
    1,1, -1,1, 22,10, -22,1, -21,10, -23,10, 21,2, 23,2, 0,0,    // +bFa=177
    43,1, 45,1, 0,0,           // wN=186
    44,1, -21,1, -23,1 ,0,0,   // wPh=189
    -44,1, 21,1, 23,1, 0,0,    // bPh=193
    22,10, -21,10, -23,1, 0,0, // wQl=197
    22,10, -21,1, -23,10, 0,0, // wQr=201
    -22,10, 21,10, 23,1, 0,0,  // bQl=205
    -22,10, 21,1, 23,10, 0,0,  // bQr=209
    22,10, 21,1, 23,1, 1,1, -1,1, -22,10, 0,0,   // wRF=213
    -22,10, -21,1, -23,1, 1,1, -1,1, 22,10, 0,0, // bRF=220
};

// first direction in 'steps' table for the various piece types
// organized in four sections, each terminated by 255: unprom white, prom white, unprom black, prom black
// first of unprom section is always king (which in reality is type 31, while unprom = 0-15 and prom = 16-31)

int
chessValues[] = { 100, 285, 290, 375, 600, -1, 700, 310, 315, 450, -1, 600, 685, 590, 675, 700, -1 },
shogiValues[]   = { 30, 150, 240, 270, 390, 450, 180, -1, 330, 288, 276, 270, 465, 540, 282, -1,  60, 270, 300, 330, 495, 570, 285, -1 },
miniValues[]    = { 60, 110, 195, 237, 243, 330,      -1, 297, 245, 240, 237, 375, 420,      -1, 120, 200, 243, 252, 315, 390, -1 },
judkinValues[]  = { 50, 115, 240, 270, 325, 390, 180, -1, 330, 245, 276, 270, 420, 480, 282, -1,  60, 220, 300, 330, 450, 510, 285, -1 },
toriValues[]  = { 60, 100, 150, 150, 237, 300, -1, 90, 0, 0, 0, 0, 0, 500, -1, 65, 150, 200, 200, 300, 400, -1 },
waValues[]  = { 30, 210, 210, 180, 210, 210, 210, 240, 270, 160, 175, 270, 300, 360, 540, 480, -1,
		330, 360, 480, 360, 240, 270, 270, 300, 330, 330, 270, 360, 540, 540, -1,
		60, 240, 300, 270, 255, 270, 240, 285, 330, 270, 285, 345, 480, 480, 630, 480, -1 };

unsigned char
chessDirs[] = { 40, 49, 4, 35, 26, 13, 255, 13, 4, 35, 26, 255, 40, 53, 4, 35, 26, 13, 255, 13, 4, 35, 26, 255 }, // K,P,N,B,R,Q / Q~,N~,B~,R~
shogiDirs[] = { 40, 69, 97, 87, 57, 35, 26, 186, 255, 57, 57, 57, 57, 31, 22, 57, 255,   // K,P,S,G,B,R,L,N / +P,+S,-,DH,DK,+N,+L
		40, 62, 94, 43, 64, 35, 26, 10, 255, 64, 64, 64, 64, 31, 22, 64, 255 },
toriDirs[]  = { 40, 69, 189, 197, 201, 42, 79, 255, 112, 0, 0, 0, 0, 168, 255,   // Ph, S, Pt, Ql, Qr, Cr, Fa / G - - - - Ea
                40, 62, 193, 205, 209, 42, 71, 255, 108, 0, 0, 0, 0, 177, 255 },
waDirs[] = { 40, 69, 97, 67, 67, 74, 59, 59, 73, 87, 57, 96, 116, 121, 135, 99, 150, 255, // CK,SP,SC,SO,FC,CM,FG,BD,VS,VW,OC,LH,SW,RR,FF,Tr,CE 
                 57, 40,135,150,213, 87,116, 57, 79, 40,  8,  26,  99, 141, 255,          //    GB,FF,CE,RF,VS,SW,VW,RB,BE,PO,HH,GS,Tr,TF
             40, 62, 94, 60, 60, 82, 66, 66, 81, 43, 64, 93, 116, 128,  34, 99, 159, 255,
                 64, 40, 34,159,220, 43,116, 64, 71, 40,  8,  26,  99, 141, 255 },

chessIDs[] = "PNBRQ",
shogiIDs[] = "PLSGBRN",
toriIDs[]  = "SPLRCF",
waIDs[]    = "POULCMGDVWHSRFXE",

chessFEN[] = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",
shogiFEN[] = "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w",
toriFEN[]  = "rpckcpl/3f3/sssssss/2s1S2/SSSSSSS/3F3/LPCKCPR w",
waFEN[]    = "hmlcvkwgudo/1e3s3f1/ppprpppxppp/3p3p3/11/11/11/3P3P3/PPPXPPPRPPP/1F3S3E1/ODUGWKVCLMH w",
miniFEN[]  = "rbsgk/4p/5/P4/KGSBR w",
judkinFEN[]= "rbnsgk/5p/6/6/P5/KGSNBR w",
euroFEN[]  = "r w",

// promotion codes for unpromoted pieces. Will be ANDed with Z_WHITE or Z_BLACK to fill promoCode[] table

#define Z_LAST     1    /* first or last rank           */
#define Z_2ND   0x66    /* must promote on last 2 ranks */
#define Z_MUST  0x78    /* must promote in entire zone  */
#define Z_WHITE 0x2B    /* bits applying to white zone  */
#define Z_BLACK 0x55    /* bits applying to black zone  */
#define Z_DOUBLE 0x80   /* where Pawns have double step */

chessProms[16] = { Z_MUST },
shogiProms[16] = { Z_MUST, Z_2ND, COLOR, 0, Z_MUST, Z_MUST, Z_2ND },
toriProms[16]  = { Z_MUST, 0, 0, 0, 0, Z_MUST },
waProms[16]    = { Z_MUST, Z_2ND, Z_MUST, Z_MUST, Z_MUST, COLOR, COLOR, Z_MUST, Z_MUST, Z_MUST, COLOR, Z_MUST, COLOR, Z_MUST };

int lances[] = { 1, 3, 0103, 0103, 3, 1, 1 }; // bitmap indicating piece types with drop restriction (LSB = Pawn) in various variants

char *betza[] = { // piece defs for sending to GUI
  NULL, // suppresses setup command
  "",
  "",
  NULL,
  ",P& fW,L& fWbF,U& fWbF,C& fFsW,G& fFvW,M& fFvW,D& fFbsW,V& FfW,W& WfF,O& fR,H& fRbW2,S& sRvW,F& BfW,E& vRfF3bFsW,R& fRFbW,X& FAvWvD"
  ",+P& WfF,+L& vRfF3bFsW,+U& BfW,+C& vRfFsW,+G& sRvW,+M& FfW,+D& WfF,+V& FfsW,+W& K,+O& K,+H& vN,+S& R,+F& BvRsW,+R& FAvWvD",
  ",S& fW,P& fDbF,L& fRbrBblF,R& fRblBbrF,C& FvW,F& FfsW,+S& fAbD,+F& fBbRbF2fsW",
};

char *ptc[] = { // XBoard 4.9 piece-to-char table
  NULL,
  "P.BR.S...G.+.++.+Kp.br.s...g.+.++.+k",
  "PNBR.S...G.++++.+Kpnbr.s...g.++++.+k",
  NULL,
  "P..^S^FV..^LW^OH.F.^R.E....R...D.GOL^M..^H.M.C.^CU.^W../.......^V.^P.^U..^DS.^GXK"
  "p..^s^fv..^lw^oh.f.^r.e....r...d.gol^m..^h.m.c.^cu.^w../.......^v.^p.^u..^ds.^gxk",
  "S.....F..........^F.P........^S....L........C......RKs.....f..........^f.p........^s....l........c......rk"
};

char *pstType[] = {
  "",
  "307716 777718",
  "3077160 7777187",
  "3077160 7777187",
  "3055676777060100 77100787777801",
  "770076 100001"
};

typedef struct {
  int files, ranks, hand, zone;
  char *name;
  unsigned char *pieces, *fen, *dirs, *proms;
  signed char *codes;
  int *values;
} VariantDesc;

VariantDesc variants[] = {
  {  8,  8,  5, 1, "crazyhouse\n",  chessIDs, chessFEN,  chessDirs, chessProms, chessCodes, chessValues },
  {  5,  5,  5, 1, "minishogi\n",   shogiIDs, miniFEN,   shogiDirs, shogiProms, shogiCodes, miniValues },
  {  6,  6,  6, 2, "judkinshogi\n", shogiIDs, judkinFEN, shogiDirs, shogiProms, shogiCodes, judkinValues },
  {  9,  9,  7, 3, "shogi\n",       shogiIDs, shogiFEN,  shogiDirs, shogiProms, shogiCodes, shogiValues },
  { 11, 11, 16, 3, "crazywa\n",     waIDs,    waFEN,     waDirs,    waProms,    shogiCodes, waValues    }, 
  { 11, 11, 16, 3, "11x17+16_chu\n",waIDs,    waFEN,     waDirs,    waProms,    shogiCodes, waValues    }, 
  {  7,  7,  6, 2, "torishogi\n",   toriIDs,  toriFEN,   toriDirs,  toriProms,  toriCodes,  toriValues  },
};

#define TORI_NR 6 /* must correspond to index of torishogi (which must be last) in table above! */

// info per piece type. sometimes indexed by negated holdings count instead of piece
#define pieceKey   (rawKey+1)
#define PST        (rawPST+1)
signed char *rawPST[COLOR+1]; // PST[-1...95] indexed by 'mutation', which is -1 for drops
unsigned int rawKey[COLOR+1];
unsigned int squareKey[22*11];

// piece-square tables. White and black tables interleave. The first two pairs are (0, center) and (hand1, ???)
#define center   (pstData + 22*11)
#define hand1    (pstData + 22*11)    /* beware: uses off-board part only */
#define kingPST  (pstData + 22*11*2)  /* King and Queen PST               */
#define pawnPST  (pstData + 22*11*3)  /* from here interleaved (white, black) */
#define knightPST (pstData + 22*11*4)
#define owlPST    (pstData + 22*11*5)
#define promoPST  (pstData + 22*11*6) /* +90 in zone, for strong promotion */
#define generalPST (pstData + 22*11*7)/* +30 in central zone */
#define rookPST   (pstData + 22*11*8) /* +45 in zone */

signed char pstData[22*11*9];  // actual tables (for now 9 pairs)

int
MyRandom ()
{
    return (rand() >> 10) + rand() + (rand() << 10) + (rand() << 20); 
}

void
EngineInit ()
{
    int i, r, f;
    for(i=1; i<sizeof(steps); i+=2) { // pack range encoding differently
	steps[i] = steps[i] >> 4 | steps[i] << 3 & 127;
	if(steps[i] & 7) steps[i] -= 8;
    }
    // set tables for on-the-fly Zobrist key creation as pieceKey[piece]*squareKey[sqr]
    for(r=0; r<11; r++) for(f=0; f<11; f++) {
	int sqr = 22*r + f;
	squareKey[sqr] = MyRandom(); squareKey[sqr+11] = MyRandom() << 16; // low 16 bits 0 for holdings squares
    }
    for(r=WHITE; r<COLOR; r++) pieceKey[r] = MyRandom();
    // for drops the from-key will be pieceKey[-1]*squareKey[typeLocation]
    pieceKey[-1] = MyRandom() << 16; // clear lowest 16 bits to make sure lowest 32 of product are zero
    PST[0] = pstData; PST[-1] = hand1; // PST for empty squares
printf("init done\n");
}

void
ClearBoard ()
{
    int i, r, f;
    for(i=-2*22-2; i<13*22-2; i++) board[i] = -1; // boundary guards (or holdings counters)
    for(r=0; r<nrRanks; r++) for(f=0; f<nrFiles; f++) board[22*r+f] = 0; // playing area
    for(f=0; f<11; f++) pawnCount[f] = 0xF0;      // Pawn occupancy per file
    for(i=0; i<512+100; i++) repKey[i] = 0;        // game-history hash
    promoGain[WHITE+30] = promoGain[BLACK+30] = 0;// danger measure of in-hand pieces
}

char *pieces, *startPos;

void
GameInit (char *name)
{
    int v, *ip, i, color, r, f, zone;
    unsigned char *moves, *codes; char c, *p;

    // determine variant parameters
    if(sscanf(name, "%dx%d+%d_%c", &f, &r, &i, &c) == 4) {
	for(v=TORI_NR; --v>0; ) if(variants[v].files == f && variants[v].ranks == r && variants[v].hand == i) break;
    } else
    for(v=TORI_NR; --v>0;) if(!strcmp(name, variants[v].name)) break;
    nrFiles = variants[v].files;
    nrRanks = variants[v].ranks;
    zone    = variants[v].zone;
    maxDrop = variants[v].hand - 1;
    pieces  = variants[v].pieces;
    moves   = variants[v].dirs;
    startPos= variants[v].fen;
    codes   = variants[v].proms;
    lanceMask = lances[v];
    perpLoses = v; // this works for now, as only zh allows perpetuals

    if((p = betza[v])) { // configure GUI for this variant
	printf("setup (%s) %dx%d+%d_%s %s 0 1", ptc[v], nrFiles, nrRanks, maxDrop+1, (v == 4 ? "chu" : "shogi"), startPos);
	while(*p) { if(*p == ',') printf("\npiece "); else printf("%c", *p); p++; }
	printf("\n");
    }

    maxDrop += (v==2 || v==1); // Judkins & mini-Shogi have dummy Lance

    // board
    ClearBoard();
    boardEnd = specials = 22*nrRanks;
    frontier = (nrRanks < 7 ? 22 : nrRanks == 7 ? 2*22 : 3*22);
    killZone = boardEnd - (nrRanks > 6 ? frontier + 2*22 : 0);
    impasse  = boardEnd - (nrRanks > 6 ? 3*22 : 2*22);
    frontierPenalty = (nrRanks > 7 ? 2 : 1);
    killPenalty     = (nrRanks > 7 ? 8 : 2);

    for(i=0; i<COLOR; i++) PST[i] = pstData;

    for(r=0; r<11; r++) {
	for(f=0; f<11; f++) {
	    int sqr = 22*r + f, piece = 11*r + f + WHITE - 1;
	    sqr2file[sqr] = f; sqr2file[sqr+11] = 12;
	    toDecode[sqr] = toDecode[sqr+11] = sqr;
	    promoInc[sqr] = 0; promoInc[sqr+11] = 16;
	    dropType[sqr] = 0; dropType[sqr+11] = piece + 1; // map counters to pieces
	    if(!(piece & 16) && piece < COLOR) {             // promotable piece
		handSlot[piece ^ COLOR] = sqr + 11;          // map pieces back to the counter (flipped color)
		handSlot[piece+16 ^ COLOR] = sqr + 11;       // also the piece that demotes to it
		handSlotSame[piece] = sqr + 11;              // map pieces to same-color counter
		handSlotSame[piece+16] = sqr + 11;           // also the piece that demotes to it
	    }
	    zoneTab[sqr] = 0;
	}
    }
    sqr2file[CK_DOUBLE] = sqr2file[CK_NONE] = 13; // for decoding hashed checker
#define CASTLE(R,F,RT,KT,RF) toDecode[R*22+F] = RT; dropType[R*22+F] = KT; zoneTab[R*22+F] = RF; promoInc[R*22+F] = 0; sqr2file[R*22+F] = 11;
    if(v == 0) for(f=0; f<11; f++) { // In Chess we re-assign other ranks to under-promotions in zone
	for(r=1; r<7; r++) toDecode[r*22+11+f] = f + (r < 4 ? 0 : 7*22); // redirect to last rank
	promoInc[1*22+11+f] = promoInc[6*22+11+f] += 1;
	promoInc[2*22+11+f] = promoInc[5*22+11+f] += 2;
	promoInc[3*22+11+f] = promoInc[4*22+11+f] += 3;
	handSlot[WHITE + 16 + f] = handSlot[WHITE]; // all promoted pieces demote to Pawn
	handSlot[BLACK + 16 + f] = handSlot[BLACK];
	handSlotSame[WHITE + 16 + f] = handSlotSame[WHITE]; // same for same-color captures
	handSlotSame[BLACK + 16 + f] = handSlotSame[BLACK];
	pawnBulk[WHITE] = 0; // no limit to Pawns per file in Crazyhouse
	zoneTab[1*22+f] = zoneTab[6*22+f] = Z_DOUBLE;
	// special Pawn moves
	toDecode[8*22+f] = 3*22 + f; // white double push
	toDecode[9*22+f] = 4*22 + f; // black double push
	toDecode[8*22+11+f] = 2*22 + f; // black e.p. capture
	toDecode[9*22+11+f] = 5*22 + f;
	promoInc[8*22+f] = promoInc[9*22+f] = promoInc[8*22+11+f] = promoInc[9*22+11+f] = 0;
	// castlings
	CASTLE(8, 10, 5, 6, 7) // K-side
	CASTLE(9, 10, 7*22+5, 7*22+6, 7*22+7)
	CASTLE(8, 21, 3, 2, 0) // Q-side
	CASTLE(9, 21, 7*22+3, 7*22+2, 7*22+0)
	spoiler[0*22+0] = 4;  // initial King and Rook squares marked as castling-right spoilers
	spoiler[0*22+4] = 5;
	spoiler[0*22+7] = 1;
	spoiler[7*22+0] = 8;
	spoiler[7*22+4] = 10;
	spoiler[7*22+7] = 2;
	PST[WHITE+4] = PST[BLACK+4] = kingPST+11; // Queens
	frontier = 2*22; killZone = 3*22; impasse = boardEnd;
    } else {
	pawnBulk[WHITE] = (nrRanks == 7 ? 1 : 2); // board file considered full when total reaches 2
    }
    pawnBulk[BLACK] = 4*pawnBulk[WHITE];
    maxBulk[BLACK] = 4*2; maxBulk[WHITE] = 2; // bits to test to see if file is full for specified side
    pawnBulk[-1] = 0; // used with drops, so they never take away a Pawn from any board file

    // precalculated hash keys for transferring to holdings
    handSlot[0] = 11*21 + 4;     // map empty square safely away from edges
    handSlotSame[0] = 11*21 + 4;
    // Initialize all handSlotSame to safe value first
    for(f=0; f<97; f++) if(handSlotSame[f] == 0) handSlotSame[f] = 11*21 + 4;
    for(f=WHITE; f<COLOR; f++) { // all pieces
	r = handSlot[f];         // location in holdings where piece goes (flipped color)
	handKey[f] = pieceKey[-1]*squareKey[r];
	r = handSlotSame[f];     // location in holdings for same-color captures
	handKeySame[f] = pieceKey[-1]*squareKey[r];
    }

    // move-generation tables
    for(color=0; color<=WHITE; color+=WHITE) {
	firstDir[color+31] = *moves++; // first is King
        for(i=0; *moves < 255; i++) firstDir[color+i] = *moves++; // unpromoted pieces
	moves++; // skip the sentinel
        for(i=16; *moves < 255; i++) firstDir[color+i] = *moves++; // promoted pieces
	moves++; // skip the sentinel
    }

    // promotion tables
    for(i=0; i<16; i++) { // per-piece promotion abilities (for unpromoted series only)
	promoCode[i]       = Z_WHITE & *codes;
	promoCode[i+WHITE] = Z_BLACK & *codes++;
    }
    for(r=0; r<zone; r++) { // board-size table to indicate promotion zones and force-promotion zones
	for(f=0; f<nrFiles; f++) {
	    int xr = nrRanks - 1 - r, c = Z_MUST | COLOR | (r==0)*Z_LAST | (r<2 && v!=1)*Z_2ND; // in mini-Shogi forelast not in zone
	    zoneTab[22*r  + f] = Z_BLACK & c;
	    zoneTab[22*xr + f] = Z_WHITE & c;
	}
    }

    ip = variants[v].values;
    for(i=0; *ip >= 0; i++) pieceValues[WHITE+i] = pieceValues[BLACK+i] = *ip++;       // basic
    for(i=16,ip++; *ip >= 0; i++) pieceValues[WHITE+i] = pieceValues[BLACK+i] = *ip++; // promoted
    for(i=0, ip++; *ip >= 0; i++) handVal[WHITE+i] = handVal[BLACK+i] = *ip++;         // in hand
    pawn = 2*handVal[WHITE] << 20; // used for detection of material-loosing loop
    queen = v ? 0 : 2*handVal[WHITE+4] << 20; // losing two Queens overflows
    for(i=0; i<16; i++) {
	int demoted = dropType[handSlot[WHITE+i+16]]-1; // piece type after demotion (could be Pawn, in Chess)
	handVal[WHITE+i+16] = handVal[BLACK+i+16] = pieceValues[WHITE+i+16] + handVal[demoted];   // gain by capturing promoted piece
	handBulk[WHITE+i] = handBulk[BLACK+i] = pieceValues[WHITE+i]/80;
	handBulk[WHITE+i+16] = handBulk[BLACK+i+16] = pieceValues[demoted]/80;
    }
    for(i=0; i<16; i++) handVal[WHITE+i] = handVal[BLACK+i] += pieceValues[WHITE+i];  // gain by capturing base piece
    // For same-color captures, piece doesn't flip color, so gain is just in-hand bonus
    // Initialize all handValSame to 0 first
    for(i=0; i<96; i++) handValSame[i] = 0;
    for(i=0; i<96; i++) handKeySame[i] = 0;
    for(i=0, ip=variants[v].values; *ip >= 0; ip++); ip++; // skip to promoted values
    for(; *ip >= 0; ip++); ip++; // skip to in-hand values
    for(i=0; *ip >= 0; i++) {
	handValSame[WHITE+i] = handValSame[BLACK+i] = *ip; // in-hand value as gain for same-color capture
	ip++;
    }
    for(i=0; i<16; i++) {
	int demoted = dropType[handSlot[WHITE+i+16]]-1;  // use regular handSlot for promoted pieces
	if(demoted >= 0 && demoted < 96) {
	    handValSame[WHITE+i+16] = handValSame[BLACK+i+16] = pieceValues[WHITE+i+16] - pieceValues[demoted] + handValSame[demoted];
	}
    }
    for(i=0; i<16; i++) {
	int demoted = dropType[handSlot[WHITE+i+16]]-1; // piece type after demotion (could be Pawn, in Chess)
	promoGain[WHITE+i+16] = promoGain[BLACK+i+16] = pieceValues[WHITE+i+16] - pieceValues[demoted];
    }
    for(i=WHITE; i<COLOR; i++) vVal[i-WHITE] = (handVal[i] + pieceValues[i])/16, aVal[i-WHITE] = handVal[i]/64;
    promoGain[WHITE+31] = promoGain[BLACK+31] = 0; // King counts as unpromoted (to make castling work, where it promotes to unpromoted Rook)

    // piece-square table
    for(r=0; r<nrRanks; r++) for(f=0; f<nrFiles; f++) {
	int sqr = 22*r + f;
	center[sqr] = 8 - (f - nrFiles/2. + 0.5)*(f - nrFiles/2. + 0.5) - (r - nrRanks/2. + 0.5)*(r - nrRanks/2.+ 0.5);
    }
    for(i=0; i<16; i++) { // PST for in-hand pieces. type = -1 only occurs on (all) drops, but true type determined by (off-board) square
	PST[-1][handSlot[WHITE+i]] = PST[-1][handSlot[BLACK+i]] = handVal[WHITE+i] - 2*pieceValues[WHITE+i];
	dropBulk[handSlot[WHITE+i]] = dropBulk[handSlot[BLACK+i]] = pieceValues[WHITE+i]/80;
    }
    PST[WHITE+2] = PST[BLACK+2] = center; // Bishops
    for(f=0; f<11; f++) { // pawn tables
	int d = (nrRanks > 8 || nrRanks == 7); // larger camp
	for(r=0; r<=zone; r++) pawnPST[22*(nrRanks - 1 - r) + f] = pawnPST[22*r + f + 11] = 1.2*pieceValues[WHITE]; // in and just before zone ('7th rank')
	pawnPST[22*(nrRanks - 2 - zone) + f] = pawnPST[22*(zone + 1) + f + 11] = 0.6*pieceValues[WHITE]; // ('6th rank')
	pawnPST[22*(nrRanks - 2) + f + 11] = pawnPST[22 + f] = 10; // discourage leaving 2nd rank
	for(r=0; r<nrRanks; r++) pawnPST[22*(nrRanks - 1 - r) + f] = pawnPST[22*r + f + 11] -= (r - nrRanks/2)*3 +10;
	for(r=2+d; r<nrRanks-2-d; r++) kingPST[22*r+f] = -127; kingPST[f] = kingPST[22*(nrRanks-1)+f] = 80; kingPST[22*d+f] = kingPST[22*(nrRanks-1-d)+f] = 90;
	for(r=2+d; r<nrRanks-2-d; r++) kingPST[22*r+f+11] = -40;
	knightPST[22*3+f] = knightPST[22*4+11+f] = 12; // only used for zh
	knightPST[22*4+f] = knightPST[22*3+11+f] = 20;
	knightPST[22*5+f] = knightPST[22*2+11+f] = 17;
    }
    PST[WHITE] = pawnPST; PST[BLACK] = pawnPST + 11;
    PST[WHITE+31] = PST[BLACK+31] = kingPST;

    for(f=0; f<nrFiles; f++) for(r=0; r<nrRanks; r++) {
	int xr = nrRanks - 1 - r; double mr = (nrRanks - 1.)/2., mf = (nrFiles - 1.)/2.;
	generalPST[22*r+f] = generalPST[22*xr+f+11] = (r-mr)*5 - r*(f-mf)*(f-mf) + 10*(xr < zone) - 20*(xr == 0);
	owlPST[22*r+f]     = owlPST[22*xr+f+11]     = (r == 1)*10 + (xr < zone ? 90 : -90) + 90*(xr == zone);
	promoPST[22*r+f]   = promoPST[22*xr+f+11]   = 90*(xr < zone);
	rookPST[22*r+f]    = rookPST[22*xr+f+11]    = 45*(xr < zone);
    }
    if(nrRanks <= 6) generalPST[(nrRanks-3)*22 + nrFiles - 3] = generalPST[2*22 + 2 + 11] += 30;
    if(nrRanks <= 6) kingPST[0] = kingPST[22*(nrRanks-1) + nrFiles - 1] += 30,
		     kingPST[2*22] = kingPST[22*(nrRanks-3) + nrFiles - 1] = 0;

    for(f=0,p=pstType[v]; *p; p++,f++) if(*p == ' ') f = 15; else  PST[BLACK+f] = (PST[WHITE+f] = pstData + 22*11*(*p - '0')) + 11*(*p > '2'); 

    InitCaptureCodes(variants[v].codes);
    pinCodes = (v == TORI_NR ? 0xFF2C : 0xFF1F); // rays along which pinning is possible
}

void
PrintDBoard (char *msg, unsigned char *b, char *sep, int ranks)
{
    int r, f;
    printf("\n%s\n", msg);
    for(r=0; r<ranks; r++) for(f=0; f<22; f++) printf("%s %02x%s", (f==0 ? "#" : ""), b[22*r+f], (f == 10 ? sep : f== 21 ? "\n" : ""));
}

void
PrintPieces (char *msg, unsigned char *p, int n)
{
    int i; if(n==0) n = COLOR;
    printf("\n%s\n", msg);
    for(i=0; i<n; i++) printf(" %02x%s", p[i], (i & 15) == 15 ? "\n" : "");
}

void
PrintValues (char *msg, int *p, int n)
{
    int i; if(n==0) n = COLOR;
    printf("\n%s\n", msg);
    for(i=0; i<n; i++) printf(" %3d%s", p[i], (i & 15) == 15 ? "\n" : "");
}

void
Debug ()
{
    int i, j, r, f;
    printf("\npieceKey\n");
    for(i=0; i<96; i++) printf(" %08x%s", pieceKey[i], (i&7)==7 ?"\n" : "");
    printf("\nsquareKey\n");
    for(i=0; i<22*11; i++) printf(" %08x%s", squareKey[i], (i&7)==7 ?"\n" : "");
    printf("\ncapt_code\n");
    for(r=2; r<19; r++) for(f=2; f<19; f++) printf(" %3x%s", rawInts[22*r+f], (f== 18 ? "\n" : ""));
    printf("\ndelta_vec\n");
    for(r=2; r<19; r++) for(f=2; f<19; f++) printf(" %3d%s", rawChar[22*r+f], (f== 18 ? "\n" : ""));
    printf("\ndistance\n");
    for(r=2; r<19; r++) for(f=2; f<19; f++) printf(" %3d%s", rawByte[70*22+22*r+f], (f== 18 ? "\n" : ""));
    PrintDBoard("zoneTab:", zoneTab, "   ", 11);
    PrintDBoard("dropType:", dropType, "   ", 11);
    PrintDBoard("toDecode:", toDecode, "   ", 11);
    PrintDBoard("promoInc:", promoInc, "   ", 11);
    PrintDBoard("sqr2file:", sqr2file, "   ", 11);
    PrintPieces("handSlot:", handSlot, 0);
    PrintPieces("promoCode:", promoCode, 0);
    PrintPieces("pawnBulk:", pawnBulk, 0);
    printf("\nnearCode:\n"); for(i=0; i<96; i++) printf(" %2x%s", pieceCode[i] & 255, (i&15) == 15 ? "\n" : "");
    printf("\nfarCode:\n"); for(i=0; i<96; i++) printf(" %2x%s", pieceCode[i]>>8 & 255, (i&15) == 15 ? "\n" : "");
    PrintValues("pieceValues:", pieceValues, 0);
    PrintValues("handVal:", handVal, 0);
    PrintValues("promoGain:", promoGain, 64);
    PrintPieces("vVal:", vVal, 64);
    PrintPieces("aVal:", aVal, 64);
    PrintPieces("handBulk:", handBulk, 96);
    PrintDBoard("dropBulk", dropBulk, "   ", 11);
    PrintDBoard("hand PST:", PST[-1], "   ", 11);
    PrintDBoard("Pawn PST:", PST[WHITE], "   ", 11);
    PrintDBoard("board:", board, "   ", 11);
}

#define KEY(A, B) (pieceKey[A]*(Key) squareKey[B])

typedef struct { // 12 bytes
    unsigned int lock;
    short int score, lim;
    unsigned short int move;
    unsigned char depth;
    unsigned char flags;
    unsigned char checker;
    char age[3];
} HashEntry;

typedef struct {
    Key hashKey, newKey;
    unsigned char fromSqr, toSqr, captSqr, epSqr, rookSqr, rights;
    signed char fromPiece, toPiece, victim, savePiece, rook, mutation;
    int pstEval, newEval, bulk, tpGain;
    int move, wholeMove, depth;
    int checker, checkDir, checkDist, xking;
    int lim; // for returning upper end of score interval
} StackFrame;

typedef struct {   // move stack sectioning
    int firstMove; // start of move list for current ply
    int unsorted;  // start of unsorted tail of move list
    int nonCapts;  // index of first non-capture in move list
    int drops;     // index of first quiet drop
    int stage;     // stage of move generation (0=hash/capt/prom, 1=killer/noncapt, 2=check-drops, 3=quiet drops)
    int late;      // start of late moves
    int castlings; // end of list of board moves without castlings
    int epSqr;
    int checker;
    int safety, cBonus, hole, escape;
} MoveStack;

HashEntry *hashTable;
Key hashKey, pawnKey;
int hashMask;

static int rightsScore[] = { 0, -10, 10, 0, -10, -30, 0, -20, 10, 0, 30, 20, 0, -20, 20, 0 };

int
Evaluate (int stm, int rights)
{
    static int rays[] = {1, -1, 22, -22, 23, -23, 21, -21 };
    int k, f, s, score = 0, w, b;
    k = location[WHITE+31]; s = (k >= 1*22)*6 + 1;
    score += ((board[k+22] == WHITE) + ((board[k+22+1] == WHITE) + (board[k+22-1] == WHITE) - 2)*s)*2;
    score += (!board[k+1] + !board[k-1])*(board[k+22] != BLACK)*5;
    score -= (2*(board[k+22] == BLACK) + (board[k+44] == BLACK) + (board[k+44+1] == BLACK) + (board[k+44-1] == BLACK))*5;
    for(f=w=0; f<8; f++) { // mark squares from which 
	int v = rays[f], x = k + v;
	if(!board[x]) while(!board[x+=v]) w++;
    }
    if(k >= killZone) score -= 100;
    k = location[BLACK+31]; s = (k < boardEnd - 1*22)*6 + 1;
    score -= ((board[k-22] == BLACK) + ((board[k-22+1] == BLACK) + (board[k-22-1] == BLACK) - 2)*s)*2;
    score -= (!board[k+1] + !board[k-1])*(board[k-22] != WHITE)*5;
    score += (2*(board[k-22] == WHITE) + (board[k-44] == WHITE) + (board[k-44+1] == WHITE) + (board[k-44-1] == WHITE))*5;
    for(f=b=0; f<8; f++) { // mark squares from which 
	int v = rays[f], x = k + v;
	if(!board[x]) while(!board[x+=v]) b++;
    }
    if(k < boardEnd - killZone) score += 100;
    score *= 5;
    score -= ((board[   0] == WHITE+3 && board[0*22+1] && board[1*22]) + (board[     7] == WHITE+3 && board[     6] && board[1*22+7]))*25;
    score += ((board[7*22] == BLACK+3 && board[7*22+1] && board[6*22]) + (board[7*22+7] == BLACK+3 && board[7*22+6] && board[6*22+7]))*25;
    score -= ((board[     2] == WHITE+2 && board[1*22+1] && board[1*22+3]) + (board[     5] == WHITE+2 && board[1*22+6] && board[1*22+4]))*15;
    score += ((board[7*22+2] == BLACK+2 && board[6*22+1] && board[6*22+3]) + (board[7*22+5] == BLACK+2 && board[6*22+6] && board[6*22+4]))*15;
    score += rightsScore[rights];
    return stm == WHITE ? score + 15*b - 9*w : -score - 15*w + 9*b;
}

int
PseudoLegal (int stm, int move)
{   // used for testing killers, so we can assume the move must be pseudo-legal for the stm in some position
    int match, from = move >> 8 & 0xFF, to = move & 255;
    signed char piece = board[from], mover = move >> 16 & 0xFF;
    to = toDecode[to]; // could be double-push or castling, though
    if(board[to]) return 0;
    if(piece < 0) { // drop
	if(piece == -1) return 0; // type not in hand
	piece = dropType[from] - 1;
	if(!(piece & stm)) return 0; // piece has wrong color
        if((piece & ~COLOR) == 0 && pawnCount[sqr2file[to]] & maxBulk[stm]) return 0; // Pawn drop would over-crowd file
	return (board[to] == 0); // otherwise drop is legal on empty square (assumes drop location always legal for piece type)
    }
    if(!(piece & stm)) return 0; // piece has wrong color
    if(piece == stm) { /// Pawn
	if(stm == WHITE) return board[from+22] == 0 && (to - from == 22 || zoneTab[from] & 0x80 && to - from == 44 && board[to] == 0);
	return board[from-22] == 0 && (to - from == -22 || zoneTab[from] & 0x80 && to - from == -44 && board[to] == 0);
    }
    match = pieceCode[piece] & captCode[to - from];
    if(!match) return 0; // not aligned
    if(match & C_DISTANT) { // distant alignment
	int step = deltaVec[to - from];
	while(board[to -= step] == 0) {}   // ray scan towards mover
	return (from == to);               // legal if it reaches mover        
    } else if((move & 255) > specials) return 0; // must be castling, as Pawns were already taken care of. Forbid for now.
    return 1; // must be leaper alignment, which guarantees hit
}

void
Dump (char *s)
{int i; printf("%s\n",s); for(i=0; i<ply; i++) printf(" {%x} %s", deprec[i], MoveToText(path[i])); PrintDBoard("board", board, "   ", 11); exit(1); }

#define attacks (rawAttacks + 23)
static int attackKey, rawAttacks[13*22];
int depthLimit = MAXPLY;

int
MoveGen (int stm, MoveStack *m, int castle)
{   // generate all board moves, return 1 if King capture found amongst those
    int r, f, c = ++attackKey;
    m->firstMove = m->nonCapts =  m->late = moveSP; m->stage = 0;
    for(r=0; r<boardEnd; r+=22) for(f=0; f<nrFiles; f++) {
	int from = r + f, piece = board[from];
	if(piece & stm) {
	    int step, dir = 2*firstDir[piece-WHITE]; // steps data comes in pairs
	    while((step = steps[dir++])){ // next direction
		int range = steps[dir++], to = from, victim, inZone = zoneTab[from], d = c - (range == 11);
		do {
		    int move, promote, slot;
		    victim = board[to += step];
		    attacks[to] = d; // keep track of attacked squares (even with own piece)
		    if((victim & stm) && (victim & ~COLOR) == 31) break; // cannot capture own King
		    // Allow capturing own pieces (except King)
		    if(range & 2) { // divergent move: must be FIDE Pawn
			if(to == m->epSqr) { // reaches e.p. square: must be through diagonal move, and e.p. square is always empty
			    moveStack[--m->firstMove] = to + 4*22+11 + 44*(stm == BLACK) | from << 8 | vVal[0] << 24;
			    break;
			}
			if(!victim == (range & 1)) break; // wrong type (lsb set = capture only, cleared = move only)
			if(range > 7) { // can do double push
			    if(inZone & Z_DOUBLE && board[to+step] == 0) {   // started on Pawn rank, and square in front of it is empty
				moveStack[moveSP++] = from << 8 | to + step + 22*5; // generate now, as special move
			    }
			    range -= 8; // make sure it is not done again
			}
		    }
		    if((victim & ~COLOR) == 31) return 1; // captures King; abort!
		    move = piece << 16;   // store piece in move
		    promote = (inZone | zoneTab[to]) & promoCode[piece-WHITE];
		    if((promote & ((Z_2ND | Z_MUST ) & ~COLOR)) == 0) { // not in place where deferral forbidden
			int slot;
			if(victim) { // capture
			    slot = --m->firstMove;
			    move += vVal[victim-WHITE] - aVal[piece-WHITE] << 24; // MVV/LVA sort code
			} else { // non-capture
			    slot = moveSP++;
			}
			moveStack[slot] = move | from << 8 | to;
		    }
		    if(promote & stm) { // promotion is (also?) possible
			moveStack[--m->firstMove] = (move | from << 8 | to + 11) + (vVal[victim-WHITE] + 20 << 24); // put it amongst captures
			if(!perpLoses) { // only for FIDE Pawns
			    moveStack[--m->firstMove] = move | from << 8 | to - 11 + (stm >> 6)*44; // turn previously-generated deferral into under-promotion
			    // other under-promotions could go here (Capahouse?)
			}
		    }
		    
		    
		    if((range -= 8) <= 0) break; // range exhausted
		} while(!victim);
	
	    }
	}
    }

    r = location[stm+31]; m->castlings = moveSP; f = 0;
    if(!(stm >> 5 & castle)) { // K-side castling
	f++;
	if(board[r+1] == 0 && board[r+2] == 0) f += 2, moveStack[moveSP++] = r << 8 | 8*22+10 + 22*(stm == BLACK);
    }
    if(!(stm >> 3 & castle)) { // Q-side castling
	f++;
	if(board[r-1] == 0 && board[r-2] == 0 && board[r-3] == 0) f += 2, moveStack[moveSP++] = r << 8 | 8*22+21 + 22*(stm == BLACK);
    }
    m->cBonus = f;

    r = location[stm+31^COLOR]; // enemy King
#   define HOLE(X) (attacks[X] == c)*!board[X]
    m->hole = HOLE(r+1) + HOLE(r-1) + HOLE(r+22) + HOLE(r+21) + HOLE(r+23) + HOLE(r-21) + HOLE(r-22) + HOLE(r-23);
#   define SAFE(X) (attacks[X] == c)*!(board[X] & stm)
    m->safety = SAFE(r+1) + SAFE(r-1) + SAFE(r+22) + SAFE(r+21) + SAFE(r+23) + SAFE(r-21) + SAFE(r-22) + SAFE(r-23);
#   define ESC(X) (board[X] & stm || attacks[X] == c)
    m->escape = 8 - (ESC(r+1) + ESC(r-1) + ESC(r+22) + ESC(r+21) + ESC(r+23) + ESC(r-21) + ESC(r-22) + ESC(r-23));
    if(stm == WHITE) r = boardEnd - 1 - r;
    m->safety += (r >= 1*22) + frontierPenalty*(r >= frontier) + killPenalty*(r >= killZone) - 5*(r >= impasse);
    m->unsorted = m->firstMove; m->drops = moveSP;
    return 0;
}

void
CheckDrops (int stm, int king)
{
    int i, lowest = 0;
    if(perpLoses) lowest = !!(pawnCount[sqr2file[king]] & maxBulk[stm]); // in Shogi no Pawn if file already crowded with those
    else lowest = (stm == WHITE ? king < 2*22 : king >= 6*22);           // in zh no Pawn from back rank
    for(i=maxDrop; i>=lowest; i--) {
	int piece = stm + i, from = handSlot[piece^COLOR];
	if((signed char)board[from] < -1) { // piece type is in hand
	    int step, dir = 2*firstDir[piece-WHITE];
	    while((step = steps[dir++])) {
		int to = king, range = steps[dir++];
		if((range & 3) == 2) continue; // non-capture direction
		while(board[to-=step] == 0) {
		    moveStack[moveSP++] = to | from << 8;
		    if((range -= 8) <= 0) { if(i == 0 && (to < 22 || to >= boardEnd-22)) moveSP--; break; }
		}
	    }
	}
    }
}

void
EvasionDrops (int stm, StackFrame *f, int mask)
{
    int i, x = f->checker, v = f->checkDir, s = stm ^ COLOR;
    while(board[x+=v] == 0) { // all squares on check ray
	int last = maxDrop;
	if((mask >>= 1) & 1) continue; // suppress 'futile interpositions'
	i = zoneTab[x] & Z_LAST; // Pawn not on first & last rank (OK for zh)
	if(perpLoses) { // but it is Shogi!
	    if(!(zoneTab[x] & stm)) i = 0; // outside zone, so dropping is always allowed
	    else if(perpLoses < TORI_NR) { // Shogi variant with Lance
		i *= 2;                    // no Pawn, then also no Lance!
		last += 1 - (zoneTab[x] & (Z_2ND & ~COLOR)) + (perpLoses & 4) >> 3; // on last 2 ranks trim off Knight (not in Wa)
	    }
	    if(pawnCount[sqr2file[x]] & maxBulk[stm]) i += !i; // no Pawn in pawn-crowded file
	}
	for(; i<=last; i++) { // all droppable types
	    int piece = s + i, from = handSlot[piece];
	    if((signed char)board[from] < -1) // piece type is in hand
		moveStack[moveSP++] = from << 8 | x;
	}
    }
}

void
AllDrops (int stm)
{
    int i, mask = lanceMask;
    for(i=0; i<=maxDrop; i++) {
	int piece = stm + i, from = handSlot[piece^COLOR];
	if((signed char)board[from] < -1) { // piece type is in hand
	    int r, f, start = 0, end = boardEnd;
	    if(mask & 1) { // piece with drop limitation
		int badZone = (i == 6 ? 44 : 22); // 6 is the Knight in (Judkins) Shogi
		if(stm == BLACK || !perpLoses) start = badZone;
		if(stm == WHITE || !perpLoses) end  -= badZone;
	    }
	    for(f=0; f<nrFiles; f++) {
		if(i == 0 && pawnCount[f] & maxBulk[stm]) continue;
		for(r=start; r<end; r+=22)
		    if(board[r+f] == 0) moveStack[moveSP++] = from << 8 | f + r;
	    }
	}
	mask >>= 1;
    }
}

int
DiscoTest (int stm, int fromSqr, int king, StackFrame *f)
{
    int vec = king - fromSqr;
    int match = captCode[vec] & pinCodes;
    if(match) { // from-square is aligned
	int x = king, v = deltaVec[vec];
	while(board[x-=v] == 0) {}   // scan ray
	if(f->checker != x && !(board[x] & stm) && captCode[king-x] & pieceCode[board[x]]) { // discovered check
	    if(f->checker != CK_NONE) f->checker = CK_DOUBLE;
	    else f->checker = x, f->checkDir = v, f->checkDist = dist[x-king];
	}
    }
}

void
CheckTest (int stm, StackFrame *ff, StackFrame *f)
{
    if(ff->mutation == -2) f->checker = CK_NONE, f->checkDist = 0; else { // null move never checks
	int king = location[stm+31]; // own King
	int vec = king - ff->toSqr;
	int match = captCode[vec] & pieceCode[ff->toPiece];
	f->checker = CK_NONE; f->checkDist = 0; // assume not in check
	if(match & C_DISTANT) { // moving piece is aligned
	    int x = ff->toSqr, v = deltaVec[vec];
	    while(board[x+=v] == 0) {} // scan ray
	    if(x == king) f->checker = ff->toSqr, f->checkDir = v, f->checkDist = dist[vec]; // ray is clear, distant check
	} else if(match & C_CONTACT) f->checker = ff->toSqr, f->checkDir = 0; // contact check
	if(ff->mutation != -1) { // board move (no drop)
	    DiscoTest(stm, ff->fromSqr, king, f);
	    if(board[ff->captSqr] == 0) DiscoTest(stm, ff->captSqr, king, f); // e.p. capture can discover check as well
	}
    }
}

int
Pinned (int stm, int fromSqr, int xking)
{
    int vec = xking - fromSqr;
    int match = captCode[vec] & pinCodes;
    if(match) {
	int x = xking, v = deltaVec[vec];
	while(board[x-=v] == 0) {}
	if(!(board[x] & stm) && captCode[xking-x] & pieceCode[board[x]]) return 1; // guards & counters tests as own piece!
    }
    return 0;
}

int
SafeIP (StackFrame *f)
{   // figure out which squares are protected on the check ray
    int result = 0, v = f->checkDir, x = f->checker, mask = 1;
    while(board[x+=v] == 0) result |= (mask <<= 1)*(attackKey != attacks[x]);
    return result & ~mask;
}

int
NonEvade (StackFrame *f)
{
    if((f->fromPiece & ~COLOR) != 31) { // moves non-royal (or drops)
	int d;
	if(f->checker == CK_DOUBLE) return 1; // never helps against double check
	if(f->toSqr == f->checker) return 0;  // captures only checker: OK
	d = dist[f->checker - f->toSqr];
	if(d && deltaVec[f->toSqr - f->checker] == f->checkDir && d < f->checkDist) return 0; // interposes: OK
	if(f->fromPiece + board[f->checker] == COLOR && f->toSqr - f->fromSqr & 1) return (board[f->toSqr] != 0);
	return 1;
    }
    // king move
    return 0; // for now, defer testing to daughter node
}

int
MakeMove (StackFrame *f, int move)
{
    int to, stm;
    f->fromSqr = move >> 8 & 255;
    to = move & 255;
    f->wholeMove = move;
    f->toSqr = f->captSqr = toDecode[to];                               // real to-square for to-encoded special moves
    f->fromPiece = board[f->fromSqr];                                   // occupant or (for drops) complemented holdings count
    if(f->checker != CK_NONE && NonEvade(f)) return 0;                  // abort if move did not evade existing check
    f->mutation = (f->fromPiece >> 7) | f->fromPiece;                   // occupant or (for drops) -1
    f->toPiece = f->mutation + dropType[f->fromSqr] | promoInc[to];     // (possibly promoted) occupant or (for drops) piece to drop
    f->victim = board[f->captSqr];					// for now this is the replacement victim
    f->newEval = f->pstEval; f->newKey  = f->hashKey;			// start building new key and eval
    f->epSqr = 255;
    f->rookSqr = sqr2file[f->toSqr] + (pawnCount - board);              // normally (i.e. when not castling) use for pawnCount
    f->rook = board[f->rookSqr];					// save and update Pawn occupancy
    board[f->rookSqr] = f->rook + pawnBulk[f->toPiece] - pawnBulk[f->mutation] - pawnBulk[f->victim]; // assumes all on same file!
//printf("f=%02x t=%02x fp=%02x tp=%02x sp=%02x mut=%02x ep=%02x\n", f->fromSqr, f->toSqr, f->fromPiece, f->toPiece, f->savePiece, f->mutation, f->epSqr);
    if(to >= specials) { // treat special moves for Chess
	if(sqr2file[to] > 11) {                                         // e.p. capture, shift capture square
//printf("# e.p. %02x\n", to);
	    f->captSqr = toDecode[to-11];				// use to-codes from double pushes, which happen to be what we need
	    f->victim  = board[f->captSqr];
	    f->savePiece = board[f->toSqr];
	    board[f->captSqr] = 0;					// e.p. is only case with toSqr != captSqr where we have to clear captSqr
	} else if(sqr2file[to] < 8) {					// double push
	    int xpawn = f->toPiece ^ COLOR;				// enemy Pawn
	    if(board[f->toSqr + 1] == xpawn ||				// if land next to one
	       board[f->toSqr - 1] == xpawn ) {
		f->epSqr = (f->fromSqr + f->toSqr) >> 1;		// set e.p. rights
	    }
	} else { // castling. at this point we are set up to 'promote' a King to Rook (so the check tests sees the Rook, and UnMake restores location[K])
	    f->rookSqr = zoneTab[to];					// Rook from-square
	    f->rook = board[f->rookSqr];                                // arrange Rook to be put back on UnMake (pawnCount is never modified in chess)
	    board[f->rookSqr] = 0;					// and remove it
	    f->newEval -= PST[f->rook][f->rookSqr];
	    f->newKey  -= KEY(f->rook, f->rookSqr);
	    f->captSqr = dropType[to];					// this tabulates to-square of the King
	    f->savePiece = f->victim;					// now toSqr and captSqr are different, make sure the piece that was on toSqr goes back there in UnMake
	    f->victim = board[f->captSqr];				// should be 0, but who knows?
	    f->toPiece = f->rook;					// make sure Rook (or whatever was in corner) will be placed on toSqr
	    board[f->captSqr] = f->mutation;				// place the King
	    f->newEval += PST[f->mutation][f->captSqr] + 50;		// add 50 cP castling bonus
	    f->newKey  += KEY(f->mutation, f->captSqr);
	    location[f->mutation] = f->captSqr;				// be sure King location stays known
	}
    }
    board[f->fromSqr] = f->fromPiece - f->mutation;                     // 0 or (for drops) decremented count
    board[f->toSqr]   = f->toPiece;
    // Check if capturing own piece (same color)
    if(f->victim && (f->victim & COLOR) == (f->toPiece & COLOR)) {
	// Same-color capture: piece stays same color in hand
	board[handSlotSame[f->victim]]--; // put victim in holdings (same color)
	f->newEval += promoGain[f->toPiece] - promoGain[f->mutation]                                        + handValSame[f->victim] +
		      PST[f->toPiece][f->toSqr] - PST[f->mutation][f->fromSqr] + PST[f->victim][f->captSqr];
	f->newKey  += KEY(f->toPiece, f->toSqr) - KEY(f->mutation, f->fromSqr) - KEY(f->victim, f->captSqr) + handKeySame[f->victim];
    } else {
	// Normal capture: piece flips color
	board[handSlot[f->victim]]--; // put victim in holdings (flipped color)
	f->newEval += promoGain[f->toPiece] - promoGain[f->mutation]                                        + handVal[f->victim] +
		      PST[f->toPiece][f->toSqr] - PST[f->mutation][f->fromSqr] + PST[f->victim][f->captSqr];
	f->newKey  += KEY(f->toPiece, f->toSqr) - KEY(f->mutation, f->fromSqr) - KEY(f->victim, f->captSqr) + handKey[f->victim];
    }
//printf("# capt=%02x vic=%02x slot=%02x\n", f->captSqr, f->victim, handSlot[f->victim]);
    stm = f->toPiece & COLOR;
    f->bulk = promoGain[stm+30]; promoGain[stm+30] += handBulk[f->victim] - dropBulk[f->fromSqr];
    location[f->toPiece] = f->toSqr;
    checkHist[moveNr+ply+1] = f->checker;

    return 1;
}

void
UnMake (StackFrame *f)
{
    board[f->rookSqr] = f->rook;      // restore either pawnCount or (after castling) Rook from-square
    board[f->toSqr]   = f->savePiece; // put back the regularly captured piece (for castling that captured by Rook)
    board[f->captSqr] = f->victim;    // differs from toSqr on e.p. (Pawn to-square) and castling, (King to-square) and should be cleared then
    board[f->fromSqr] = f->fromPiece; //          and the mover
    // Restore victim to hand (same location it was taken from)
    if(f->victim && (f->victim & COLOR) == (f->toPiece & COLOR)) {
	board[handSlotSame[f->victim]]++; // same-color capture
    } else {
	board[handSlot[f->victim]]++;     // normal capture (flipped color)
    }
    promoGain[(f->toPiece & COLOR)+30] = f->bulk;
    location[f->fromPiece] = f->fromSqr;
}

#define PATH 0
//ply==0 || path[0]==0x0017b1 && (ply==1 || (ply==2))

int
HisComp (const void *x, const void*y)
{
    return history[*(int *)y & 0xFFFF] - history[*(int *)x & 0xFFFF];
}
 
int
Search (int stm, int alpha, int beta, StackFrame *ff, int depth, int reduction, int maxDepth)
{
    MoveStack m; StackFrame f; HashEntry *entry;
    int oldSP = moveSP, *pvStart = pvPtr, oldLimit = depthLimit, oldAna;
    int killer1 = killers[ply][0], killer2 = killers[ply][1], hashMove;
    int bestNr, bestScore, startAlpha, startScore, resultDepth, iterDepth=0, originalReduction = reduction;
    int hit, hashKeyH, ran=0, ipMask=0;
    int curEval, anaEval, score, upperScore, minScore = -INF, maxScore = INF;

    // legality
    int earlyGen = (ff->fromPiece == stm+31); // King was moved
    if(ply > 90) { if(DEBUG) Dump("maxply"); ff->depth = 0; ff->lim = ff->newEval-150; return -ff->newEval+150; }
    f.xking = location[stm+31]; // opponent King, as stm not yet toggled
    if(!earlyGen && ff->mutation > 0) { // if other piece was moved (not dropped!), abort with +INF score if it was pinned
	if(Pinned(stm, ff->fromSqr, f.xking) ||
	   board[ff->captSqr] == 0 && Pinned(stm, ff->captSqr, f.xking)) { // also check 'e.p. pin'
	    ff->depth = MAXPLY; ff->lim = -INF; return INF;
	}
    }


    // some housekeeping
    stm ^= COLOR;
    f.hashKey =  ff->newKey;
    f.pstEval = -ff->newEval;
    f.rights  =  ff->rights | spoiler[ff->toSqr] | spoiler[ff->fromSqr];
    m.epSqr   =  ff->epSqr; // put in m, because MoveGen needs it

    // hash probe
    hashKeyH = f.hashKey >> 32;
    entry = hashTable + (f.hashKey + (stm + 9849 + f.rights)*(m.epSqr + 51451) & hashMask);
    if(entry->lock == hashKeyH || (++entry)->lock == hashKeyH || (++entry)->lock == hashKeyH || (++entry)->lock == hashKeyH) { // 4 possible entries
	int score = entry->score, d = entry->depth;
	signed char p;

	f.checker = entry->checker; f.checkDist = 0;
	if(f.checker != CK_NONE) { // in check; restore info needed in evasion test
	    if(sqr2file[f.checker] != 12) f.checkDir = 0; else { // off-board represents on-board distant check
		int vec = location[stm+31] - (f.checker -= 11);
		f.checkDir = deltaVec[vec];
		f.checkDist = dist[vec];
	    }
	    reduction = 0; // checks are not reduced
	}
	if(score >= beta || entry->lim <= alpha || entry->score == entry->lim) { // only take hash cuts from fully resolved results, unless they fail low or high
	    d += (score >= beta & entry->depth >= LMR)*reduction; // fail highs need to satisfy reduced depth only, so we fake higher depth than actually found
	    if((score > alpha && d >= depth || d >= maxDepth) && ply) { // sufficient depth
		ff->depth = d + 1; ff->lim = -entry->score; return entry->lim; // depth was sufficient, take hash cutoff
	    }
	}
	hashMove = entry->move;
	hit = 1;
	p = board[hashMove>>8&255];
	if(hashMove && ((p & stm) == 0 || p == -1)) {
	    if(DEBUG) printf("telluser bad hash move %16llx: %s\n", f.hashKey, MoveToText(hashMove));
	    hashMove = 0; f.checker = CK_UNKNOWN;
	}
    } else hit = hashMove = 0, f.checker = CK_UNKNOWN;

    moveSP += 48;  // create space for non-captures
    if(earlyGen) { // last moved piece was King, e.p. capture or castling
	int kingCapt;
	if(!ff->victim) board[ff->toSqr] = stm + 31 ^ COLOR; // kludge: after castling we temporarily make Rook a second King to catch passing through check
	kingCapt = MoveGen(stm, &m, f.rights);
	board[ff->toSqr] = ff->toPiece; // undo kludge damage
	if(kingCapt) { moveSP = oldSP; ff->depth = MAXPLY; ff->lim = -INF; return INF; } // make sure we detect if he moved into check
    }

    if((++nodeCount & 0xFFF) == 0) abortFlag |= TimeIsUp(3); // check time limit every 4K nodes
    curEval = f.pstEval + Evaluate(stm, f.rights);
    alpha -= (alpha < curEval); //pre-compensate delayed-loss bonus
    beta  -= (beta <= curEval);
    if(ff->checker == CK_NONE) killers[ply+1][0] = killers[ply+1][1] /* = killers[ply+1][2]*/ = 0;
    else if(ply > 0) killers[ply+1][0] = killers[ply-1][0], killers[ply+1][1] = killers[ply-1][1]; // inherit killers after check+evasion
    if(-INF >= beta) { moveSP = oldSP; ff->depth = MAXPLY; ff->lim = INF-1; return INF; }


    // check test
    if(f.checker == CK_UNKNOWN) CheckTest(stm, ff, &f); // test for check if hash did not supply it
    if(f.checker != CK_NONE) {
	depth++, maxDepth++, reduction = originalReduction = 0 /*, killers[ply][2] = -1*/; // extend check evasions
	if(earlyGen && f.checkDist && maxDepth <= 1) ipMask = SafeIP(&f);
    } else if(depth > LMR) {
	if(depth - reduction < LMR) reduction = depth - LMR; // never reduce to below 'LMR' ply
	depth -= reduction;
    } else reduction = originalReduction = 0;
    checkHist[moveNr+ply+1] = f.checker;
    oldAna = anaSP; anaSP += (ff->checker != CK_NONE); // node after evasion: accept new analogy
    anaEval = curEval;

    // stand pat or null move
    startAlpha = alpha; startScore = -INF;
    if(depth <= 0) { // QS
	if(ff->checker != CK_NONE && ff->tpGain > 0) anaEval = 50-INF; // forbid stand pat if horizon check tossed material
	if(anaEval > alpha) {
	    if(anaEval >= beta) { ff->depth = 1; ff->lim = -anaEval - (anaEval < curEval); moveSP = oldSP; anaSP = oldAna; return INF; } // stand-pat cutoff
	    alpha = startScore = anaEval; maxDepth = 0; // we will not fail low, so no extra iterations
	}
	if(maxDepth <= 0) {
	    if(board[toDecode[hashMove&255]] == 0) hashMove = 0;
#ifdef IDQS
	    if(ply >= depthLimit) { ff->depth = 1; ff->lim = -anaEval-150; moveSP = oldSP; anaSP = oldAna; return anaEval + 150; } // hit depth limit; give hefty stm bonus
	    if(depthLimit == MAXPLY) depthLimit = ply+10; // we just entered pure QS; set up depth limit
#endif
 	}
    } else if(curEval >= beta && f.checker == CK_NONE) {
	int nullDepth = depth - 3;
	int eva = (ff->checker != CK_NONE) && !oldAna && nullDepth >= 0; // first evasion in branch, and depth of null-move search was higher before the check
	nullDepth += eva; // reduce one less than normal after first evasion, to make sure we see same threats after spite check
	if(nullDepth < 0) nullDepth = 0;
	f.mutation = -2; // kludge to suppress testing for discovered check
	f.newEval = f.pstEval;
	f.newKey = f.hashKey;
	f.epSqr = -1; f.fromSqr = f.toSqr = f.captSqr = 1; f.toPiece = board[1];
	deprec[ply] = maxDepth << 16 | depth << 8; path[ply++] = 0;
	score = -Search(stm, -beta, 1-beta, &f, nullDepth, 0, nullDepth);
	ply--;
	if(score >= beta) { ff->depth = f.depth + originalReduction + 3; ff->lim = -beta-1; moveSP = oldSP; anaSP = oldAna; return INF; }
    }

    // move generation
    if(!earlyGen) { // generate moves if we had not done so yet
	if(MoveGen(stm, &m, f.rights)) { // impossible (except for hash collision giving wrong in-check status)
	    if(DEBUG) Dump("King capture"); ff->depth = MAXPLY; ff->lim = -INF; moveSP = oldSP; anaSP = oldAna; return INF;
	}
 	if(f.checkDist && maxDepth <= 1) ipMask = SafeIP(&f);
    }
    if(hashMove) moveStack[--m.firstMove] = hashMove; // put hash move in front of list (duplicat!)
    if(f.checker != CK_NONE) moveSP = m.drops = m.castlings; // clip off castlings when in check
    if(ff->checker != CK_NONE) { // last move was evasion; see if we have counter move
	int move = mateKillers[(ff->wholeMove & 0xFFFF) + (stm - WHITE << 11)];
	if(move && (move>>16 & 0xFF) == f.xking && (move>>24 & 0xFF) == board[toDecode[move & 0xFF]] && PseudoLegal(stm, move)) { // counter move is pseudo-legal and matches position
	    moveStack[moveSP++] = moveStack[m.nonCapts]; moveStack[m.nonCapts] = move & 0xFFFF; m.late = ++m.nonCapts; // make room and put with captures (lowest sort key)
	}
    }

    if(depth <= 0 && anaEval == curEval) {
	int bonus = (m.safety + 3*m.hole)*(10 + m.safety + m.hole + promoGain[stm+30] /*- 0*promoGain[COLOR-stm+30]*/) + 15*m.cBonus;
	bonus += (m.escape < 2 ? 200 - m.escape*100 : 0);
	if(bonus > 900) bonus = 900; // S4
	bonus += bonus >> 1;
	int newEval = curEval + bonus;
	if(newEval > alpha) {
	    if(newEval >= beta) { ff->depth = 1; ff->lim = -newEval; moveSP = oldSP; depthLimit = oldLimit; anaSP = oldAna; return INF; } // stand-pat cutoff
	    alpha = startScore = newEval; maxDepth = 0; // we will not fail low, so no extra iterations
	}
   }

  again: // QS IDD loop
    do { // IID loop
	int curMove, highDepth;
	iterDepth++;
	highDepth = (iterDepth > depth ? iterDepth : depth) - 1; // reply depth for high-failing moves
	pvPtr = pvStart; *pvPtr++ = 0; // empty PV
	bestScore = upperScore = startScore; bestNr = 0; // kludge: points to 0 entry in moveStack
	resultDepth = MAXPLY;
	m.stage &= 3;
	for(curMove=m.firstMove; m.stage<4; curMove++) {
	    int score;

	    // sort section
	    if(curMove >= m.unsorted) {
		if(curMove < m.nonCapts) { // captures: extract best
		    unsigned int i, bestNr = curMove, bestCapt = moveStack[curMove];
		    for(i=curMove+1; i<m.nonCapts; i++) if(moveStack[i] > bestCapt) bestCapt = moveStack[bestNr=i]; // find best
		    moveStack[bestNr] = moveStack[curMove]; moveStack[curMove] = bestCapt; // swap it to front
		    m.unsorted = curMove + 1; // sorted set now includes move
		} else {
		    if(maxDepth <= 0) { 
			resultDepth = 0; if(upperScore < anaEval) upperScore = anaEval;
			if(m.stage) break;
			moveSP = curMove;
			if(ff->checker != CK_NONE && depthLimit != MAXPLY && oldLimit == MAXPLY) { // last move before QS was evasion
			    CheckDrops(stm, f.xking); // also do check drops
			    if(moveSP > curMove) { m.stage = 3; m.unsorted = moveSP; curMove--; continue; }
			}
			if(depthLimit == MAXPLY || oldLimit != MAXPLY || checkHist[moveNr+ply-1] == CK_NONE) break;
			if(killer1 && PseudoLegal(stm, killer1)) moveStack[moveSP++] = moveStack[m.late], moveStack[m.late++] = killer1; // try (possibly inherited) killers
			if(killer2 && PseudoLegal(stm, killer2)) moveStack[moveSP++] = moveStack[m.late], moveStack[m.late++] = killer2;
			CheckDrops(stm, f.xking);
			if(curMove >= moveSP) break;
			m.stage = 3; m.unsorted = moveSP;
			curMove--;
			continue;
		    } // in QS we stop after captures
		    switch(m.stage) { // we reached non-captures
		      case 0:
			if(f.checker == CK_NONE) { // do not use killers when in check
			    if(killer1 && PseudoLegal(stm, killer1)) moveStack[moveSP++] = moveStack[m.late], moveStack[m.late++] = killer1; // insert killers
			    if(killer2 && PseudoLegal(stm, killer2)) moveStack[moveSP++] = moveStack[m.late], moveStack[m.late++] = killer2; // (original goes to end)
			}
			m.drops = moveSP;
			// here we can sort based on history
			if(moveSP > m.late) qsort(moveStack + m.late, moveSP - m.late, sizeof(int), &HisComp);
			m.stage = 1; if(moveSP > curMove) break;
		      case 1:
			if(f.checker != CK_NONE) {
			    m.stage |= 4; // when in check we stop after evasion drops
			    if(f.checkDist == 0) continue; // but there cannot be any for contact/double checks
			    EvasionDrops(stm, &f, ipMask); // at d <= 1 suppress futile interpositions
			    if(moveSP <= curMove) continue; // no avail
			    m.stage = 3; break;
			}
			CheckDrops(stm, f.xking);
			m.stage = 2; if(moveSP > curMove) break;
		      case 2:
			if(iterDepth > 1) { // quiet drops only at depth >= 2
			    AllDrops(stm);
			    m.stage = 3; if(moveSP > curMove) break;
			}
		      case 3:
			m.stage |= 4; continue; // this value of m.stage terminates the loop over moves
		    }
		    m.unsorted = moveSP; // set to return here when done with the current list
		}
	    }

	    // make move
	    if(MakeMove(&f, moveStack[curMove])) { // aborts if fails to evade existing check

		// repetition checking
		int index = (unsigned int)f.newKey >> 24 ^ stm << 2; // uses high byte of low (= hands-free) key
		while(repKey[index] && (repKey[index] ^ (int)f.newKey) & 0xFFFFF) index++;
		int oldRepKey = repKey[index], oldRepDep = repDep[index];
		if(oldRepKey && ff->mutation != -2) { // key present in table: (quasi-)repetition
		    int gain = (f.newEval << 20) - (repKey[index] & 0xFFF00000);
		    if(gain == 0) { // true repeat
			score = 0;
			if(perpLoses) { // repetitions not always draw
			    int i, d = repDep[index];
			    for(i=moveNr+ply-1; i>=d; i-=2) if(checkHist[i+1] == CK_NONE) break;
			    if(i < d) score = -INF; // we deliver a perpetual, so lose
			    else {
				for(i=moveNr+ply-2; i>=d; i-=2) if(checkHist[i+1] == CK_NONE) break;
				if(i < d) score = INF-1; // we are suffering a perpetual, so lose
				else if(perpLoses == 1) score = (stm == WHITE ? -INF : INF-1); // mini-Shogi, sente loses
				else if(perpLoses == TORI_NR) score = -INF; // Tori Shogi, repeating loses
			    }
			   
			}
		    }
		    else if(gain == pawn  || gain == queen  || gain >= (400<<20)) score = INF-1;  // quasi-repeat with extra piece in hand
		    else if(gain == -pawn || gain == -queen || gain <= (-400<<20)) score = 1-INF; // or with one piece less
		    else goto search;// traded one hand piece for another; could still lead somewhere
		    f.lim = score; f.depth = (score >= beta ? highDepth+1 : iterDepth); // minimum required depth
		    *pvPtr = 0; // fake that daughter returned empty PV
		} else { // not a repeat: search it
		    int lmr;
		  search:
		    lmr = (curMove >= m.late) + (curMove >= m.drops);
		    f.tpGain = f.newEval + ff->pstEval;     // material gain in last two ply
		    if(ply==0 && randomize && moveNr < 10) ran = (alpha > INF-100 || alpha <-INF+100 ? 0 : (f.newKey*ranKey>>24 & 31)- 16);
		    repKey[index] = (int)f.newKey & 0xFFFFF | f.newEval << 20; repDep[index] = ply + moveNr; // remember position
		    // recursion
		    deprec[ply] = (f.checker != CK_NONE ? f.checker : 0)<<24 | maxDepth<<16 | depth<< 8 | iterDepth; path[ply++] = moveStack[curMove] & 0xFFFF;
		    score = -Search(stm, -beta, -alpha+ran, &f, iterDepth-1, lmr, highDepth);
		    if(ran && score < INF-100 && score > 100-INF) score += ran, f.lim += ran;
		    ply--;

		    repKey[index] = oldRepKey; repDep[index] = oldRepDep;
		}

		// unmake
		UnMake(&f);

	    } else score = f.lim = -INF, f.depth = MAXPLY;
if(PATH){
int m=moveStack[curMove];
printf("%d:%d:%d %2d. %08x %c%d%c%d %6d %6d %6d\n",ply,depth,iterDepth,curMove,m,(m>>8&255)%22+'a',(m>>8&255)/22+1,toDecode[m&255]%22+'a',toDecode[m&255]/22+1,f.pstEval,score,bestScore);
}

	    if(abortFlag) { moveSP = oldSP; depthLimit = oldLimit; anaSP = oldAna; return -INF; }

	    // minimaxing
	    if(f.depth < resultDepth) resultDepth = f.depth;
	    if(f.lim > upperScore) upperScore = f.lim;

	    if(score > bestScore) {
		bestScore = score;
		if(score > alpha) {
		    int *tail;
		    alpha = score; bestNr = curMove;
		    history[moveStack[curMove] & 0xFFFF] += iterDepth*iterDepth;
		    if(score > INF-100 && curMove >= m.nonCapts)
			mateKillers[(ff->wholeMove & 0xFFFF) + (stm - WHITE << 11)] = moveStack[curMove] & 0xFFFF | f.xking << 16 | board[f.toSqr] << 24; // store mate killers
		    if(score >= beta) { // beta cutoff
			if(f.checker == CK_NONE && curMove >= m.nonCapts && moveStack[curMove] != killers[ply][1])
			    killers[ply][0] = killers[ply][1], killers[ply][1] = moveStack[curMove];
			resultDepth = f.depth;
			upperScore = INF; goto cutoff; // done with this node
		    }
		    tail = pvPtr; pvPtr = pvStart; *pvPtr++ = moveStack[curMove]; // alpha < score < beta: move starts new PV
		    while(*pvPtr++ = *tail++); // copy PV of daughter node behind it (including 0 sentinel)
		    if(ply == 0) { // in root we print this PV
			int xbScore = (score > INF-100 ? 100000 + INF - score : score < 100-INF ? -100000 - score - INF : score);
			printf("%d %d %d %d", iterDepth, xbScore, ReadClock(0)/10, nodeCount);
			for(tail=pvStart; *tail; tail++) printf(" %s", MoveToText(*tail));
			printf("\n"); fflush(stdout);
			ff->move = moveStack[bestNr];
		    }
		}
	    }
	}   // move loop

	// stalemate correction

	// self-deepening
	if(resultDepth > iterDepth) iterDepth = resultDepth; // unexpectedly deep result (from hashed daughters?)
	if(reduction && iterDepth == depth) depth += reduction, originalReduction = reduction = 0; // no fail high, start unreduced re-search on behalf of parent
	if(iterDepth >= depth && alpha > startAlpha ) break; // move is PV; nominal depth suffices
	alpha = startAlpha; // reset alpha for next iteration

	// put best in front
	if(bestNr > m.firstMove) {
	    int bestMove = moveStack[bestNr];
	    if(bestNr == m.firstMove+1) moveStack[bestNr] = moveStack[m.firstMove]; else m.firstMove--; // swap first two, or prepend duplicat
	    moveStack[bestNr = m.firstMove] = bestMove;
	} else m.late += (m.late == bestNr); // if best already in front (or non-existing), just make sure it is not reduced

    } while(iterDepth < maxDepth && (ply || !TimeIsUp(1)));   // IID loop

    if(upperScore == -INF) { // we are mated!
	if(perpLoses) {     // Shogi
	    if(ff->fromSqr == handSlot[stm]) bestScore = upperScore = INF; // mated by Pawn drop, so we win!
	} else if(f.checker == CK_NONE) bestScore = upperScore = 0;        // stalemate in zh is draw
    }

  cutoff:

#ifdef IDQS
    if(depthLimit != MAXPLY) { // we are in iteratively deepening QS
	if(bestScore < minScore) { bestScore = minScore; if(upperScore < bestScore) upperScore = bestScore; } // when we aspired with the previous result, fail high and low must mean score is on edge
	if(upperScore > maxScore) { upperScore = maxScore; if(upperScore < bestScore) bestScore = upperScore; }
	if(oldLimit == MAXPLY && bestScore != upperScore && bestScore < beta && upperScore > alpha) { // we are in the root of QS and the score is unresolved
	    depthLimit+=10; alpha = startScore = curEval; iterDepth = 0; // increase depth limit and search again
	    goto again;
	}
    }
#endif

    // delayed-loss bonus
    bestScore += (bestScore < curEval);
    upperScore += (upperScore < curEval);
    resultDepth -= (f.checker != CK_NONE); // store unextended depth

    // hash store
    if(!hit) { // replacement
//	if(searchNr - entry[-3].age > 2) entry -= 3; else { // replace primary hit if stale
	{
	    HashEntry *entry2 = entry - 3;
	    entry2 += (entry2[0].depth > entry2[1].depth);
	    entry -= (entry[0].depth > entry[-1].depth);
	    if(entry->depth > entry2->depth) entry = entry2;
	}
    }
    entry->lock = hashKeyH;
    entry->move = moveStack[bestNr]; // if no move was found, bestNr = 0, and moveStack[0] contains INVALID
    entry->score = bestScore;
    entry->lim = upperScore;
    entry->depth = resultDepth;
    entry->flags = (bestScore > alpha)*H_LOWER + (bestScore < beta)*H_UPPER;
    entry->checker = f.checker + 11*(f.checkDist != 0); // encode distant check as off-board checker

    // return results
    moveSP = oldSP; anaSP = oldAna; pvPtr = pvStart; depthLimit = oldLimit;
    ff->depth = resultDepth + 1 + originalReduction; // report valid depth as seen from parent
    ff->lim = -bestScore;
    return upperScore;
}

int gameMove[MAXMOVES];  // holds the game history
int stm = WHITE;

// Some routines your engine should have to do the various essential things
void PonderUntilInput(int stm);         // Search current position for stm, deepening forever until there is input.

int
TimeIsUp (int mode)
{ // determine if we should stop, depending on time already used, TC mode, time left on clock and from where it is called ('mode')
  int t = ReadClock(0), targetTime, panicTime;
  if(timePerMove >= 0) {                               // fixed time per move
    targetTime = panicTime = 10*timeLeft - 30;
  } else if(mps) {                                     // classical TC
    int movesLeft = -(moveNr >> 1);
    while(movesLeft <= 0) movesLeft += mps;
    targetTime = 10*(timeLeft - 30) / (movesLeft + 2);
    panicTime  = 50*(timeLeft - 30) / (movesLeft + 4);
  } else {                                             // incremental TC
    targetTime = 10*timeLeft / 40 + inc;
    panicTime = 5*targetTime;
  }
  if(nrFiles > 9) targetTime *= 0.66;    // Wa takes longer
  switch(mode) {
    case 1: return (t > 0.6*targetTime); // for starting new root iteration
    case 2: return (t > targetTime);     // for searching new move in root
    case 3: return (t > panicTime);      // during search
  }
  return 0; // unreachable; added to silence warning
}

StackFrame undoInfo;

int
Setup (char *fen)
{ // very flaky FEN parser
  static char castle[] = "KkQq-", startFEN[200];
  int pstEval, rights, stm = WHITE, i, p, sqr = 22*(nrRanks-1); // upper-left corner
  ClearBoard();
  if(!fen) fen = startFEN; else strcpy(startFEN, fen); // remember start position, or use remembered one if not given
  if(strchr(fen, '*') && strlen(fen) > 30) fen += 18;  // Alien-Edition Wa implementation; strip off leading 11/11/***********/
  rights = 15; pstEval = 0;               // no castling rights, balance score
  hashKey = pawnKey = 0;                  // clear hash keys
  while(*fen) {                                       // parse board-field of FEN
    if(*fen == ' ' || *fen == '[') break;
    if(*fen == '/') sqr = 22*(sqr/22) - 22; else      // skip to (start of) next rank
    if(*fen == '*') fen++; else                       // ignore dark squares
    if(*fen <= '9' && *fen >= '0') {
      int n = atoi(fen); sqr += n; fen += (n > 9);    // skip given number of squares (and second digit of 10 or 11)
    } else {
      int color, prom, n;
      fen += prom = (*fen == '+');                    // remember promotion prefix
      p = *fen & ~32; color = *fen - p + WHITE;       // convert to upper case and determine color
      prom |= n = (fen[1] == '~'); fen += n;          // remember promotion suffix
      i = 0; while(pieces[i] && pieces[i] != p) i++;  // identify piece type
      if(p == 'K') i = 31;                            // K is not in list, and (royal) piece 31 in any variant
      if(p == 'Q' && *fen == '~') i = 0;              // Q~ is +P, not +Q
      i |= color + 16*prom;                           // adjust type for color and promotion
      board[sqr] = i; location[i] = sqr;              // place piece
      hashKey += KEY(i, sqr);                         // update hash key
      pstEval += (color & WHITE ? 1 : -1)*(PST[i][sqr]+pieceValues[i]);// update PST eval (white POV)
      pawnCount[sqr2file[sqr]] += pawnBulk[i];        // Pawn occupancy per file
      sqr++;
    }
    fen++;
  }
  while(*fen == ' ') fen++; // skip white
  if(*fen == '[') {         // holdings
    if(*++fen == '-') fen++; else
    while(*fen && (p = *fen) != ']') {
      int color;
      p &= ~32; color = *fen++ - p + WHITE;
      i = 0; while(pieces[i] && pieces[i] != p) i++;  // identify piece type
      i |= color;                                     // adjust type for color
      sqr = handSlot[i ^ COLOR];                      // determine counter location
      board[sqr]--;                                   // count piece in hand      
      hashKey += handKey[i ^ COLOR];                  // update hash key
      pstEval += (color & WHITE ? 1 : -1)*(handVal[i] - pieceValues[i]); // update PST eval (white POV)
      promoGain[color+30] += handBulk[i];
    }
    fen++;    // skip closing bracket
  }
  if(*++fen == 'b') stm = BLACK, pstEval *= -1; // black to move; flip eval
  while(*++fen == ' ');
  while(*fen) {
    i=0; while(castle[i] && castle[i] != *fen) i++;
    if(i > 3) break;
    rights &= ~(1<<i);
    fen++;
  }

  undoInfo.rights = rights; undoInfo.fromSqr = undoInfo.toSqr = undoInfo.captSqr = 44; undoInfo.toPiece = board[44]; // kludge to prevent spoiling of rights
  undoInfo.newEval = (stm == WHITE ? pstEval : -pstEval);
  undoInfo.newKey = hashKey;

  lastGameMove = 0;  // TODO: use FEN e.p. rights to fake double-push here
  return stm;
}

char *
MoveToText (int move)
{
  static char buf[20], pieceID[] = "+nbrq";
  int promo = '\0', from = move >> 8 & 0xFF, to = move & 0xFF;
  int inc = promoInc[to], castle = (nrFiles < 11 && to%11 == 10);
  to = toDecode[to];
  if(inc > 0) promo = pieceID[inc - 16]; // move is promotion
  else if(castle) to = 2*to - from;     // move is castling, and 'to' indicates Rook; calculate King to-square
  if(promo == '+' && !perpLoses) promo = 'q';
  if(from%22 > 10) sprintf(buf, "%c@%c%d", pieces[dropType[from]-1&~COLOR], 'a'+(to%22), 1+(to/22));   // move is drop
  else sprintf(buf, "%c%d%c%d%c", 'a'+(from%22), 1+(from/22), 'a'+(to%22), 1+(to/22), promo);
  return buf;
}

int
ParseMove(int stm, char *s)
{
  char prom = 0, f, f2, piece;
  int m, r, r2, i, from;
  if(sscanf(s, "%c@%c%d", &piece, &f, &r) == 3) { // drop
    f -= 'a', r--;
    m = 22*r + f;
    for(i=0; pieces[i]; i++) if(pieces[i] == piece) break;
    m += handSlot[COLOR - stm + i] << 8;
  } else {
    sscanf(s, "%c%d%c%d%c", &f2, &r2, &f, &r, &prom);
    f -= 'a'; f2 -= 'a'; r--, r2--;
    m = 22*r + f + ((from = 22*r2 + f2) << 8);
    if(prom == '\n') prom = 0;
    if(prom) {        // promotion suffix
      int in = (stm == WHITE ? -22 : 22); // inward board step
      m += 11;       // move to-square off-board on same rank (good for '+' and 'q')
      switch(prom) { // under-promotions must encode choice by rank (they always occur on last rank)
        case 'n': m += in; break;
        case 'b': m += 2*in; break;
        case 'r': m += 3*in; break;
	case '=': m -= 11;   break; // no promotion after all
      }
    } else if(board[from] == stm + 31 && (f - f2 > 1 || f2 - f > 1)) { // castling
      m = 22*8+10 + (f > f2 ? 0 : 11) + 22*(r2 == 7) + (from << 8);
    } else if(board[from] == stm) { // Pawn moves
      if(!(r - r2 & 1)) m += 5*22;  // steps even nr of ranks, so must be double push
      else if(f - f2 && !board[22*r+f]) m += 22*((r^1) - r + 5) + 11; // diagonal to empty: e.p.
    }
  }
printf("# move = %08x\n", m);
  return m;
}

// Some global variables that control your engine's behavior
int ponder;
int resign;         // engine-defined option
int contemptFactor; // likewise

#ifdef WIN32 
#    include <windows.h>
#    define CPUtime 1000.*clock
#else
#    include <sys/time.h>
#    include <sys/times.h>
#    include <unistd.h>
     int GetTickCount() // with thanks to Tord
     {	struct timeval t;
	gettimeofday(&t, NULL);
	return t.tv_sec*1000 + t.tv_usec/1000;
     }
#endif

int
ReadClock (int start)
{
  static int startTime;
  int t = GetTickCount();
  if(start) startTime = t;
  return t - startTime; // msec
}

int
SetMemorySize (int n)
{
  static int oldSize;
  if(n == oldSize) return 0;       // nothing to do
  oldSize = n;                     // remember current size
  if(hashTable) free(hashTable);   // throw away old table
  for(hashMask = (1<<26)-1; hashMask*sizeof(HashEntry) > n*1024*1024; hashMask >>= 1);    // round down nr of buckets to power of 2
  hashTable = (HashEntry*) calloc(hashMask+4, sizeof(HashEntry));
printf("# memory allocated, mask = %x\n", hashMask+1);
  return !hashTable;               // return TRUE if alocation failed
}

int
RootMakeMove(int move)
{
  int index;
  // irreversibly adopt incrementally updated values from last move as new starting point
  MoveStack m;
  int e = undoInfo.pstEval, checker;
  gameMove[moveNr] = move; // remember game
  undoInfo.pstEval = -undoInfo.newEval; // (like we initialize new Stackframe in daughter node)
  undoInfo.hashKey = undoInfo.newKey;
  undoInfo.rights |= spoiler[undoInfo.fromSqr] | spoiler[undoInfo.toSqr];
  undoInfo.checker = CK_NONE; // make sure move will not be rejected
  moveSP = 48;
  checker = (MoveGen(stm ^ COLOR, &m, undoInfo.rights) ? CK_DOUBLE : CK_NONE); // test if we are in check by generating opponent moves
  moveSP = 0; // and throw away those moves
  MakeMove(&undoInfo, move);
  checkHist[moveNr+1] = checker;
  undoInfo.tpGain = e + undoInfo.newEval;
  if(moveNr >= 19 && !perpLoses) PST[WHITE+1] = knightPST, PST[BLACK+1] = knightPST + 11;
  // store in game history hash table
  index = (unsigned int)undoInfo.newKey >> 24 ^ stm << 2; // uses high byte of low (= hands-free) key
  while(repKey[index] && (repKey[index] ^ (int)undoInfo.newKey) & 0xFFFFF) index++; // find empty slot
  repKey[index] = (int)undoInfo.newKey & 0xFFFFF | undoInfo.newEval << 20; // remember position
  repDep[index] = moveNr;
  stm ^= COLOR; moveNr++;
  return 1;
}

void
TakeBack (int n)
{ // reset the game and then replay it to the desired point
  int last;
  stm = Setup(NULL); // uses FEN saved during previous Setup
  last = moveNr - n; if(last < 0) last = 0;
  for(moveNr=0; moveNr<last; moveNr++) RootMakeMove(gameMove[moveNr]);
}

void PrintResult(int stm, int score)
{
  if(score == 0) printf("1/2-1/2\n");
  if(score > 0 && stm == WHITE || score < 0 && stm == BLACK) printf("1-0\n");
  else printf("0-1\n");
}

int engineSide=NONE;         // side played by engine
int ponderMove;
char inBuf[800];

void
ReadLine ()
{
  int i, c;
  if(inBuf[0]) return; // buffer already holds a backlogged command;
  for(i = 0; (c = getchar()) != EOF && (inBuf[i++] = c) != '\n'; );
  inBuf[i] = 0;
}

int
DoCommand (int searching)
{
  char command[80];

  while(1) { // usually we break out of this loop after treating one command

    ReadLine();                   // read one line into inBuf (or retrieve backlogged)
printf("# command: %s\n", inBuf);
    if(!*inBuf) exit(0);          // EOF, terminate
    sscanf(inBuf, "%s", command); // extract the first word
    *inBuf = 0;                   // and already mark the buffer as empty

    // recognize and execute 'easy' commands, i.e those that can be executed during search
    if(!strcmp(command, "quit"))    { exit(0); }  // exit immediately
    if(!strcmp(command, "otim"))    { continue; } // move will follow immediately, wait for it
    if(!strcmp(command, "time"))    { sscanf(inBuf+4, "%d", &timeLeft); continue; }
    if(!strcmp(command, "easy"))    { ponder = OFF; return 0; }
    if(!strcmp(command, "hard"))    { ponder = ON;  return 0; }
    if(!strcmp(command, "post"))    { postThinking = ON; return 0; }
    if(!strcmp(command, "nopost"))  { postThinking = OFF;return 0; }
    if(!strcmp(command, "random"))  { randomize = ON;    return 0; }
    if(!strcmp(command, "."))       { return 0; } // periodic update request; ignore for now
    if(!strcmp(command, "option")) { // setting of engine-define option; find out which
      if(sscanf(inBuf+7, "Resign=%d",   &resign)         == 1) return 0;
      if(sscanf(inBuf+7, "Contempt=%d", &contemptFactor) == 1) return 0;
      return 1;
    }

    if(searching) {
      if(!strcmp(command, "usermove")) { return 1; } // TODO during search we just test for ponder hit
      *inBuf = *command;                             // backlog command (by repairing inBuf)
      return 1;                                      // and request search abort
    }

    // the following commands can (or need) only be done when not searching
    if(!strcmp(command, "force"))   { engineSide = NONE;    return 1; }
    if(!strcmp(command, "analyze")) { engineSide = ANALYZE; return 1; }
    if(!strcmp(command, "exit"))    { engineSide = NONE;    return 1; }
    if(!strcmp(command, "level"))   {
      int min, sec=0;
      sscanf(inBuf, "level %d %d %d", &mps, &min, &inc) == 3 ||  // if this does not work, it must be min:sec format
      sscanf(inBuf, "level %d %d:%d %d", &mps, &min, &sec, &inc);
      timeControl = 60*min + sec; timePerMove = -1;
      return 1;
    }
    if(!strcmp(command, "protover")){
      printf("feature ping=1 setboard=1 colors=0 usermove=1 memory=1 debug=1 reuse=0 sigint=0 sigterm=0 myname=\"CrazyWa " VERSION "\"\n");
      printf("feature variants=\"crazyhouse,shogi,minishogi,judkinshogi,torishogi,euroshogi,crazywa,"
				"5x5+5_shogi,6x6+6_shogi,7x7+6_shogi,11x17+16_chu\"\n");
      printf("feature option=\"Resign -check 0\"\n");           // example of an engine-defined option
      printf("feature option=\"Contempt -spin 0 -200 200\"\n"); // and another one
      printf("feature done=1\n");
      return 1;
    }
    if(!strcmp(command, "sd"))      { sscanf(inBuf+2, "%d", &maxDepth);    return 1; }
    if(!strcmp(command, "st"))      { sscanf(inBuf+2, "%d", &timePerMove); return 1; }
    if(!strcmp(command, "memory"))  { if(SetMemorySize(atoi(inBuf+7))) printf("tellusererror Not enough memory\n"), exit(-1); return 1; }
    if(!strcmp(command, "ping"))    { printf("pong%s", inBuf+4); return 1; }
//  if(!strcmp(command, ""))        { sscanf(inBuf, " %d", &); return 1; }
    if(!strcmp(command, "new"))     { engineSide = BLACK; stm = WHITE; maxDepth = MAXPLY-2; randomize = OFF; moveNr = 0; ranKey = GetTickCount() | 0x1001; return 1; }
    if(!strcmp(command, "variant")) { GameInit(inBuf + 8); Setup(startPos); return 1; }
    if(!strcmp(command, "setboard")){ engineSide = NONE;  stm = Setup(inBuf+9); return 1; }
    if(!strcmp(command, "undo"))    { TakeBack(1); return 1; }
    if(!strcmp(command, "remove"))  { TakeBack(2); return 1; }
    if(!strcmp(command, "go"))      { engineSide = stm;  return 1; }
    if(!strcmp(command, "hint"))    { if(ponderMove != INVALID) printf("Hint: %s\n", MoveToText(ponderMove)); return 1; }
    if(!strcmp(command, "book"))    {  return 1; }
    // completely ignored commands:
    if(!strcmp(command, "xboard"))  { return 1; }
    if(!strcmp(command, "computer")){ return 1; }
    if(!strcmp(command, "name"))    { return 1; }
    if(!strcmp(command, "ics"))     { return 1; }
    if(!strcmp(command, "accepted")){ return 1; }
    if(!strcmp(command, "rejected")){ return 1; }
    if(!strcmp(command, "?"))       { return 1; } // 'move now'
    if(!strcmp(command, "p"))       { Debug(); return 1; }

    if(!strcmp(command, "b"))       {  PrintDBoard("board:", board, "   ", 11); return 1; }
    if(!strcmp(command, ""))  {  return 1; }
    if(!strcmp(command, "usermove")){
      int move = ParseMove(stm, inBuf+9);
      if(move == INVALID) printf("Illegal move\n");
      else if(!RootMakeMove(move)) printf("Illegal move\n");
      else {
        ponderMove = INVALID;
      }
      return 1;
    }
    printf("Error: unknown command\n");
  }
  return 0;
}

int
main ()
{
  int score, i;

  EngineInit(); SetMemorySize(1);  // reserve minimal hash to prevent crash if GUI sends no 'memory' command
  GameInit("zh"); Setup(startPos); // to facilitate debugging from command line

  while(1) { // infinite loop

    fflush(stdout);                 // make sure everything is printed before we do something that might take time

    if(stm == engineSide) {         // if it is the engine's turn to move, set it thinking, and let it move
{int i;for(i=0; i<=hashMask+3; i++) /*if(hashTable[i].score != hashTable[i].lim)*/ hashTable[i].lock = 0;}
      nodeCount = forceMove = undoInfo.move = abortFlag = 0; ReadClock(1);
      for(i=0;i<1<<16;i++) history[i] = 0; //>>= 1;
      for(i=0;i<1<<17;i++) mateKillers[i] = 0;
      score = Search(stm^COLOR, -INF, INF, &undoInfo, maxDepth, 0, maxDepth);

      if(!undoInfo.move) {             // no move, game apparently ended
        engineSide = NONE;             // so stop playing
        PrintResult(stm, score);
      } else {
        RootMakeMove(undoInfo.move);   // perform chosen move (stores it in lastGameMove and changes stm)
        printf("move %s\n", MoveToText(undoInfo.move)); // and output it
      }
    }

    fflush(stdout); // make sure everything is printed before we do something that might take time
#if 0
    // now it is not our turn (anymore)
    if(engineSide == ANALYZE) {       // in analysis, we always ponder the position
        PonderUntilInput(stm);
    } else
    if(engineSide != NONE && ponder == ON && moveNr != 0) { // ponder while waiting for input
      if(ponderMove == INVALID) {     // if we have no move to ponder on, ponder the position
        PonderUntilInput(stm);
      } else {
        int newStm = MakeMove(stm, ponderMove);
        PonderUntilInput(newStm);
        UnMake(ponderMove);
      }
    }
#endif
    DoCommand(0);
  }
  return 0;
}
