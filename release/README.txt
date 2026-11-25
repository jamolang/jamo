Jamo
====

A small programming language for Windows that makes real .exe files.

Install
-------

1. Unzip this whole folder anywhere.
2. Double-click install.cmd. It copies jamo.exe into a folder of its own and adds it to your PATH.
   It needs no administrator, and only changes things for your Windows user.
3. Open a NEW terminal (like Windows Terminal or PowerShell) and type:  jamo version

Your first program
------------------

  jamo new hello            Makes a folder called hello with a program in it
  jamo run hello\main.jamo  Builds it and runs it

Or start from a small game that already works:

  jamo new --game my_game
  jamo run my_game\main.jamo

GUIDE.md teaches the whole language step by step, and the examples folder has twelve programs to read and change.
GUIDE.md is plain text, so any text editor can open it.

Sharing a game
--------------

  jamo build --window my_game\main.jamo

makes my_game\main.exe, which runs on any 64-bit Windows 10 or 11 PC with nothing installed. Send it along with any
pictures or sounds it loads. When someone downloads an .exe from the internet, Windows may say "Windows protected your PC",
since it doesn't know who made it. They can click "More info", then "Run anyway".

Uninstall
---------

Double-click uninstall.cmd. It removes jamo.exe and takes it off your PATH. Your own programs stay where they are.
