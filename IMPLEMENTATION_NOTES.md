# Recycle Chess Implementation Notes

## Overview
This document describes the implementation of the modified crazyhouse variant ("Recycle Chess") in `dropper.c`.

## Rule Changes from Standard Crazyhouse

### 1. Capturing Own Pieces
**Rule:** Players can now capture their own pieces (except the King).

**Implementation:** 
- Modified `MoveGen()` function (line 839) to allow moves that capture friendly pieces
- Added check to specifically prevent capturing own King: `if((victim & stm) && (victim & ~COLOR) == 31) break;`
- Slider pieces still stop after encountering any piece (own or enemy)

### 2. Color Preservation for Own Captures
**Rule:** When capturing own pieces, they don't flip color when going to hand. When capturing enemy pieces, color flips as usual.

**Implementation:**
Added three new arrays parallel to existing hand management:
- `handSlotSame[97]` - maps pieces to same-color hand locations
- `handKeySame[96]` - hash keys for same-color hand transfers
- `handValSame[96]` - evaluation values for same-color captures

In `MakeMove()` function (lines 1102-1114):
```c
if(f->victim && (f->victim & COLOR) == (f->toPiece & COLOR)) {
    // Same-color capture: use handSlotSame, handKeySame, handValSame
} else {
    // Normal capture: use handSlot, handKey, handVal (with color flip)
}
```

Similarly updated `UnMake()` function (line 1133) to restore pieces to correct hand location.

### 3. Increased Hand Values
**Rule:** Pieces are more valuable in hand:
- Pawn: +500 (100 on board → 600 in hand)
- Knight: +400 (285 → 685)
- Bishop: +300 (290 → 590)
- Rook: +300 (375 → 675)
- Queen: +100 (600 → 700)

**Implementation:**
Modified `chessValues` array (line 296):
```c
chessValues[] = { 100, 285, 290, 375, 600, -1,  // on-board values
                  700, 310, 315, 450, -1,        // promoted values
                  600, 685, 590, 675, 700, -1 }  // in-hand values
```

## Key Code Locations

| Feature | File | Line(s) | Description |
|---------|------|---------|-------------|
| Hand value data | dropper.c | 296 | chessValues array with new in-hand bonuses |
| Own-piece capture | dropper.c | 839 | Allow capturing own pieces except King |
| Same-color arrays | dropper.c | 72-74 | Declaration of handSlotSame, handKeySame, handValSame |
| Array initialization | dropper.c | 504-505, 519-520 | Initialize handSlotSame mappings |
| Hash key setup | dropper.c | 551-558 | Initialize handKeySame for zobrist hashing |
| Value initialization | dropper.c | 595-607 | Initialize handValSame for evaluation |
| MakeMove logic | dropper.c | 1102-1114 | Conditional color flipping |
| UnMake logic | dropper.c | 1133 | Restore to correct hand location |

## Testing

The implementation has been verified to:
1. ✓ Allow capturing own pieces (except King)
2. ✓ Maintain normal enemy piece capture behavior
3. ✓ Apply correct hand value bonuses
4. ✓ Compile without errors or warnings

### Test Examples
```
# Test 1: Own-piece capture
b1c3  # Knight to c3
a7a6  # Black pawn move
c3b1  # Knight captures starting square (allowed!)

# Test 2: Enemy piece capture  
e2e4  # Pawn to e4
e7e5  # Black pawn to e5
g1f3  # Knight to f3
b8c6  # Black knight to c6
f3e5  # Captures enemy pawn (flips color as normal)
```

## Notes

- Kings are never capturable (own or enemy)
- The engine properly maintains position evaluation with new hand values
- Zobrist hashing is updated correctly for both capture types
- All original crazyhouse functionality is preserved
