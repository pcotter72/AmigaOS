RAChoice 1.3 / 1.4 / 1.5 - CaffeineOS build
===========================================

RAChoice by Philippe CARPENTIER (flype44) shows a ReAction requester
(requester.class, V47) from an AmigaDOS script, like RequestChoice.  This is
the CaffeineOS build of his RAChoice 1.2, MPL-2.0 like the original
(LICENSE).  Upstream: github.com/flype44/AmigaOS, directory RAChoice, commit
0d782f7 (7.8.2025); the unchanged 1.2 files are on the main branch of this
repository.

CaffeineOS uses it in Prefs/Network/TCPStackSelector,
Prefs/Network/Wlan_Device_Selector and S:NetworkInfo.

What changed (all in RAChoice.c, which is generated from the 1.2 source by a
script, port_rachoice.py, that is not part of this branch):

 1. The hang.  RAChoice gave requester.class the process's pr_WindowPtr as
    the parent window.  pr_WindowPtr = (APTR)-1 means "no DOS requesters";
    a program that sets it passes it on to everything it starts (dos.library:
    NP_WindowPtr "default is same as parent").  With -1 the requester never
    opened and RAChoice waited for ever inside requester.class.  -1 is now
    passed as NULL (no reference window).
 2. PUBSCREEN is honoured (it was read but not used).
 3. FreeArgs() after use (the ReadArgs() result was never freed).
 4. Built with m68k-amigaos-gcc 6.5.0b and libnix (build.sh) instead of
    SAS/C: proto/ headers, the RM_OPENREQ tag list built as an array (the old
    varargs helper passed &Tag1), Stricmp() from utility.library, the two
    class bases initialised so libnix does not auto-open wrong names.
 5. Without a reference window a screen is always passed (PUBSCREEN, else
    the default public screen).  requester.class before V47 requires one
    ("REQUIRED if or_Window is not supplied", NDK 3.9 classes/requester.h);
    OS 3.9 has requester.class 42.

Measured on the A2000 (an Amiga with a PiStorm (Emu68), OS 3.2.3,
requester.class 47.11), 31.10.2026, started from amiagent (pr_WindowPtr
$FFFFFFFF, measured with winptr):
  - RAChoice 1.1 directly: hung (twice, TaskDump: semaphore wait in
    requester.class).
  - RAChoice 1.1 with pr_WindowPtr set to NULL (winptr): window opened in
    1 s, returned 0.
  - RAChoice 1.3 directly: window opened in 1 s, returned 0.
  - RAChoice 1.3 called as Wlan_Device_Selector calls it (FILE, IMAGE=
    ChangeWPA.info, SET RESULT, "_SelectDevice|_Cancel"): window in 1 s,
    Cancel -> RESULT=0, SelectDevice -> RESULT=1.
  - Wlan_Device_Selector 2.4 run from amiagent under Miami with 1.3 as
    C:RaChoice: all its windows opened; it quit Miami and stopped the network.
  - The final build again on the A2000: direct 0, Cancel 0, SelectDevice 1.
Measured on the A1200 (an Amiga with a PiStorm32-lite (Emu68), OS 3.9,
requester.class 42.10), final build, started from amiagent: direct, window in
1 s, returned 0; called as the selector calls it, Cancel 0, SelectDevice 1.
Started from DOpus / Workbench 1.1 worked already; nothing changes there.

1.4 (11.11.2026): warnings and error texts are printed only when the output
is a console, so a script that captures the answer ("RAChoice >ENV:x ...")
gets the number and nothing else.

1.5 (13.11.2026) - the picture.  Shipped with the CaffeineOS 9318 preview update
of 14.11.2026.
  Every IMAGE= in CaffeineOS scripts names a Workbench icon (.info).
  Measured with a small probe program (imgprobe), 13.11.2026:
    - a classic .info is no picture for datatypes: NewDTObject fails on OS 3.9
      (no icon datatype, IoErr 212/2000) and on a 3.2.3 CaffeineOS system
      (icon.datatype 47.3 present, IoErr 103); bitmap.image's
      BITMAP_SourceFile (what 1.1 used) fails on both too.  A PNG-format
      .info loads on both.  icon.library (51.4 on both) reads either kind.
    - requester.class shows a picture only from V47 on (REQ_Image is marked
      V47 in NDK 3.2 classes/requester.h and is missing from the NDK 3.9
      header).  OS 3.9 has requester.class 42.10: it always shows its own
      "?" there.
  What 1.5 does (new file ownreq.c, three small hooks in RAChoice.c made by
  port_rachoice.py):
    - if the datatype load fails and the name ends in ".info", the icon is
      read with icon.library;
    - requester.class V47+: the icon is drawn into a bitmap and given to the
      class as its image, like a picture.  (Drawn through a plain RastPort
      into a work bitmap with 16 pixels to spare: through a layer on the
      bitmap icon.library 51.4 draws nothing - measured on the A600.)
    - requester.class below V47: when there is a picture (datatype bitmap or
      icon) RAChoice opens a requester of its own (Intuition + GadTools) that
      shows it.  Same answers as the class (rightmost button 0, the others
      1, 2 ... from the left), hot keys from the first underscore of a button
      text, further underscores shown as text (as the class does), body
      lines clipped to the window, window never larger than the screen.
      Ctrl-C to RAChoice answers like the rightmost button.
    - requester.class below V47 and a text larger than the screen, with or
      without a picture: the own requester too.  The class's requester is
      then cut off at the screen edge and its buttons cannot be reached with
      the mouse (measured: The Butcher's 25-line ReadMe on a 640x256 screen
      with requester.class 42.10 - only the hot keys still worked).  The own
      requester never gets larger than the screen: long lines are wrapped
      at blanks, and when there are more rows than fit, a last row says so
      and the cursor keys (Shift = a page), space and backspace move through
      the text; the buttons are always visible.  "Larger than the screen" is
      judged from the own layout plus 24 pixels of width (calibrated from
      three window pairs, see ownreq.c).  On V47 nothing changes: how its
      class handles such a text was not looked at.
    - without a picture and with a text that fits, with IMAGE=INFO/WARNING/
      ..., with a file that cannot be loaded either way, or if the own
      window cannot be opened: exactly what 1.4 did.
    - icon.library below V44 (plain 3.1): the icon's classic image is drawn
      with DrawImage (not tested: no such system here).
  Tested 13.11.2026 from RAM:, started below amiagent, answers captured to a
  file, buttons clicked through the agent (the test scripts and logs are kept
  with the CaffeineOS project):
    WinUAE, Kickstart 45.57 / Workbench 45.3, requester.class 42.10, and
    an A600 with a PiStorm (Emu68), Kickstart 47.115 / Workbench 47.5,
    requester.class 47.11 - on both: no IMAGE, IMAGE=WARNING, a missing
    .info, the classic icon, a PNG icon, three buttons (middle = 2), FILE,
    SETENV, PUBSCREEN, a text larger than the screen, DEBUG; the updater's
    reboot requester and OnlineUpdater's "Update|Cancel" with the answer
    captured ("1<LF>" / "0<LF>", nothing else).  WinUAE also: hot key,
    Ctrl-C.  1.4 beside it on both machines shows no icon for the same line.
  Also run with the 9318 updater's own three requester lines (live script
  lines 27, 407, 2025, verbatim, SET RESULT / DLCHOICE / CHOICE read back
  in the script): 1.4 and 1.5 on WinUAE, line 27 with 1.5 on the A600 - the
  variable holds the number each time, the long body is shown whole
  (shots/upd_winuae_27_14_vs_15_and_407.png, upd_a600_line27_15.png).
  Stack of the caller (test build, pattern fill, deepest use in bytes):
    OS 3.9 / WinUAE: class requester as in 1.4: 3100; own requester: 3076
      (exec 45.20's StackSwap() leaves $BAD1BAD3 near the bottom of the
      stack on the class path - that mark is not counted);
    OS 3.9 / A1200 with a PiStorm32-lite (Emu68): class requester
      3280-3462; own requester 3256-3422;
    OS 3.2.3 / A600: class requester 3332-3438 with the final build
      (3332-3514 with the earlier build of the same evening,
      cases_a600_323.log), with or without a picture.
    (final build: cases2_*.log; the spread between runs of the same case
    on Emu68 is 106-182 bytes and was not looked into.)
  So 1.5 needs no more stack than 1.4 did.  A real 4096-byte stack could not
  be produced through the agent (commands kept the 131072-byte stack even
  after "Stack 4096") - not tested; by the figures about 1000 bytes would
  be left on WinUAE and about 600 on the two Emu68 machines, for 1.4 and
  1.5 alike.
  Keys, final build (md5 93df3721...), WinUAE, 640 x 256 public screen, The
  Butcher's ReadMe (27 rows, 12 shown), states compared as pixel hashes of
  the window (tools/run_keys.py, keys_winuae.log): cursor down moves the
  text, cursor up brings the top back, space = a page, backspace back,
  Shift+cursor down = the same state as space, Shift+cursor up back, cursor
  up at the top changes nothing; hot key g (left button) answers 1, hot key
  n (right button) "0<LF>", Ctrl-C (Break <n> C) "0<LF>".
  A public screen that is behind another screen (tools/run_behind.py,
  behind_winuae.log, WinUAE, OS 3.9): neither 1.4 (requester.class 42.10)
  nor 1.5 (own requester) brings it to the front; with both the hidden
  requester still takes its hot key.  No difference, nothing changed.
  icon.library on a system that is being updated from 9317: the archive
  9317+16.lha (10.12.2025; SDH0/SDH1/UserFat files, not a
  complete system - it has no requester.class, for one) has
  Libs/icon.library 51.4.600 (HB020) and icon.libraryTC020 51.4.600 - V51,
  so the V44 path of 1.5 would apply.  What a 9317 card really has loaded
  was not read (no 9317 system here tonight).  Tested here only with
  51.4.604: read from the library in memory on the A600 and on WinUAE
  (imgprobe); the A1200 reports "51.4 (HB020)" and its LIBS:icon.library
  has the same md5 as WinUAE's (ce943d3e...).
  It matters: all three requester lines of the 9318 updater carry
  IMAGE="${AICON}" (SYS:System/Emu68/img/OnlineUpdater.info, a classic
  icon; the test runs of those lines above had AICON set).  Lines 27 and
  407 run before the unpack with the old system's RAChoice; the reboot
  requester (line 2113 of the prepared build) runs after it with the new
  one.  On a system whose requester.class is below V47 that is the own
  requester, drawn with the icon.library the 9317 system loaded.
  The own requester on a Kickstart 47 machine (two 9317 systems with
  Kickstart 47.115 updated on 13.11.2026; if their requester.class is
  still the 3.9 one they get the own requester on V47 Intuition and
  GadTools): run on the A600 (Kickstart 47.115, OS 3.2.3) with the TEST
  build and RAChoiceTest=1, the updater's reboot line verbatim
  (tools/run_own_v47.py, own_v47_a600.log, shots/own_v47_a600_reboot.png):
  icon shown, left button -> CHOICE 1, hot key n -> 0, stack 3144.  (With
  no picture and a text that fits, the variable does not force the own
  requester - the class is used, as in the release.)
  NOT TESTED: 1.5 on a 9317 system (icon.library 51.4.600 or whatever it
  has); the release build's own requester on a system with Kickstart 47
  and the 3.9 requester.class (the A600 run used the test build).


Build: bash build.sh              (RAChoice.c + ownreq.c -> RAChoice)
       bash build.sh test         also RAChoice.test: variable RAChoiceTest
                                  (1 = own requester on every system, 2 = it
                                  shows the bitmap drawn for V47) and a stack
                                  report line; never shipped.
RAChoice.c is generated from the 1.2 source (see above); edit ownreq.c directly.
