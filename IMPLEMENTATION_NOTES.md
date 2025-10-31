# Implementation Notes for Gothic and Capablanca Variant Rule Changes

## Summary
Modified fmax4_8.c to implement special capture and hand mechanics for Gothic Chess and Capablanca Chess variants.

## Changes Implemented

### 1. Own-Piece Capture
- **Location**: Line ~186-190 in D() function
- **Change**: Allow capturing own pieces (except King) in gothic (variant 5) and capablanca (variant 4)
- **Logic**: Check `CurrentVariant` and piece type before blocking own-piece captures

### 2. Color Flipping Rules
- **Location**: Line ~206-220 in D() function  
- **Change**: When capturing own pieces, they keep their color; when capturing enemy pieces, they flip color (as before)
- **Implementation**: Store captured piece with correct color when placing in hand

### 3. Hand Storage
- **Location**: Positions 128-159 in board array `b[]`
- **Structure**:
  - Positions 128-143: WHITE hand
  - Positions 144-159: BLACK hand
- **Encoding**: Hand pieces marked with bit 6 (value 64) to distinguish from board pieces

### 4. Piece Value Bonuses
- **Location**: Line ~191-200 in D() function
- **Bonuses for pieces in hand**:
  - Pawns: +500 centipawns (+5 pawns)
  - Knights: +400 centipawns (+4 pawns)
  - Bishops: +300 centipawns (+3 pawns)
  - Rooks: +300 centipawns (+3 pawns)
  - Queens: +100 centipawns (+1 pawn)
- **Implementation**: Check bit 6 flag and piece type, add appropriate bonus

### 5. Drop Moves
- **Location**: Line ~293-330 in D() function
- **Generation**: After normal move generation, iterate through hand pieces and try dropping on empty squares
- **Restrictions**: Pawns cannot be dropped on 1st or 8th rank
- **Notation**: 
  - Input: "P@e4" format (piece type @ square)
  - Output: "P@e4" format
- **Execution**: Remove from hand (clear bit 6), place on board

### 6. Global Variant Tracking
- **Location**: Line 107 - new global variable `CurrentVariant`
- **Purpose**: Track which variant is being played
- **Values**:
  - 4: Capablanca
  - 5: Gothic
  - Other values: standard variants

## Technical Details

### Bit Encoding for Pieces
- Bits 0-3: Piece type (0=empty, 1-2=pawn, 3=king, 4=knight, 5=bishop, 6=rook, 7=queen, etc.)
- Bit 4 (16): Color (0=WHITE, 16=BLACK)
- Bit 5 (32): Virgin flag (piece hasn't moved)
- Bit 6 (64): Hand flag (piece is in hand) - **NEW**
- Bit 7 (128): Reserved/future use

### Move Encoding for Drops
- K >= 128: Drop move from hand position K
- K < 128: Regular move from board position K
- L: Destination square (always < 128 for valid drops)

## Testing Recommendations

1. Test capturing own pieces in gothic/capablanca
2. Test capturing enemy pieces (should work as before)
3. Test dropping pieces from hand
4. Test pawn drop restrictions
5. Verify piece values are correctly adjusted when in hand
6. Test undo/redo (may have limitations due to hand history not being saved)

## Known Limitations

1. Hand positions not saved in game history (HistoryBoards)
2. Material count R may not perfectly reflect hand piece values
3. Drop notation assumes XBoard/WinBoard protocol compatibility

## Files Modified

- `fmax4_8.c`: Main chess engine file
  - Added CurrentVariant global variable
  - Modified capture logic in D() function
  - Added hand storage and retrieval
  - Added drop move generation and execution
  - Added drop move parsing and output

## Compilation

```bash
gcc -O2 -D LINUX fmax4_8.c -o fmax
```

## Usage

1. Start engine with xboard protocol
2. Load gothic or capablanca variant
3. Pieces captured (both own and enemy) go to hand with appropriate color
4. Drop pieces using "P@e4" notation
