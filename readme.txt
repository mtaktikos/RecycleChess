                                FAIRY-MAX 4.8A RELEASE NOTES

Fairy-Max is a self-documented open-source engine for Chess and Chess variants. It is a derivative of the World's smallest Chess program, micro-Max, and although no special effort is made to keep the size of its source code to the utmost minimum, its humble origins make that the engine outine is still only about 100 lines of C-code. The whole source is contained in a single file (fmax4_8.c).

Fairy-Max is a WinBoard compatible engine. This means it does not display a Chess board itself, but has tu use WinBoard for that purpose, and should then be started by starting WinBoard, and instruct it to use Fairy-Max for thinking up the moves. 

Fairy-Max uses a hash table of 96 MegaByte by default. It is essential that this table fit in the memory of your computer, with sufficient room to spare for Windows and other support programs, and a posible opponent program. On old systems, the amount of DRAM might not be sufficient for this. To prevent that Fairy-Max will slow down thousandfold (with a corresponding drop in playing strength) by substituting hard disk for memory, you will have to instruct it to use a smaller hash table. To this end, you would have to replace the name Fairy-Max\fmax in the short-cut to start WinBoard in a specific mode, in the startup dialog box if you start through the WinBoard icon itself, or in the winboard.ini file by "Fairy-Max\fmax 20". (Including the quotes and space!) The 20 indicates Fairy-Max should only use 12MB hash table. (Each increment/decrement of this number would double/half the hash size, i.e. 21 would mean 24MB, etc.) It is usually OK to use about a quarter of the availble memory, even if you run Fairy-Max twice (by playing it against itself).

Fairy-Max should be possible to compile it with any C compiler. It was developed using gcc under cygwin. On this platform, it can simply be compiled with the command:

gcc -O2 -mno-cygwin fmax4_8.c -o fmax.exe

This gives you a perfectly functional executable, which you can shrink in size somewhat by using

strip fmax.exe

For compiling under Linux, you have to define the compiler swithch LINUX. This can be done by adding the
command-line option "-D LINUX" (without te quotes) in the compilation command.

If you want the executable to show up in Windows as a nice icon, rather than the standard symbol for an executable, you have to link it to an icon pictogram. I do this by the sequence of commands:

windres --use-temp-file --include-dir . fmax.rc -O coff -o fres.o
gcc -O2 -mno-cygwin -c fmax4_8.c
gcc -mno-cygwin *.o -o fmax.exe
strip fmax.exe

I have no idea if this is a good or a stupid way to do it, I have zero experience in programming Windows applications, and this was a copy-cat solution based on the WinBoard source code that seemed to work.

Have fun,
H.G. Muller


New features compared to previous releases:

* Support for a cylindrical board (used in predefined variant Cylinder Chess)
* Support for lame leapers and multi-path lame leapers.
* Support for e.p. capture of Berolina Pawns.

The supplied fmax.ini file now includes definitions for Cylinder Chess, Berolina Chess,
Falcon Chess (patented!) and Super-Chess (TM) next to the usual set (normal Chess,
Capablanca Chess, Gothic Chess, Shatranj, Courier Chess and Knightmate). Note that
Cylinder Chess and Berolina Chess can only be played in WinBoard 4.3.15 with legality testing off.

SOME NOTES ON SUPERCHESS

Super Chess is defined with the standard pieces used in the Dutch Open of this variant:
Archbishop (A), Marshall (M), Centaur (C) and Amazon (Z) (which in Super Chess are referred to as
Princess, Empress, Veteran and Amazon, respectively). But it also has definitions for the
Cannon (O), Nightrider (H), Dragon Horse (D) and and Grasshopper (G). If you include any of those
pieces in the initial setup, Fairy-Max will use them. You can instruct WinBoard to use these pieces
in stead of the default AMCZ by redefining the pieceToCharTable of WinBoard with a command-line option.
Note that the promotion rules for SuperChess are not yet implemented in this version of Fairy-Max:
the opponent can promote to anything (but if it is not Queen this will come as a complete surprise
to Fairy-Max), and Fairy-Max itself only promotes to Queen. (Even if it already has one...)
The Grasshopper can currently only be used in WinBoard when Legality testing is off.

SOME NOTES ON FALCON CHESS

Falcon Chess features a piece that is covered by a U.S. patent (#5,690,334), the Falcon, 
which is a non-leaping piece that can follow three alternative paths to any (1,3) or (2,3) 
destination. (In the sme terminology as where a Knight reaches any (1,2) destination.) 
The current version of Fairy-Max is licenced to use this piece in the way it does now. 
This does NOT imply, however, that any code you derive from Fairy-Max, (something that in 
itself you are perfectly allowed to make) is also licensed to play Falcon Chess, or use the
Falcon in another Chess variant, if any of the modification you made reflect in any way on 
its skill in playing Falcon Chess or these Falcon-containing variants.
In such a case you would have to seek a new license from the patent holder, George W. Duke.