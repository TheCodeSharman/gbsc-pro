   10 REM > TestPat
   20 REM Capture-geometry, sampling and aspect card for the GBSC-Pro.
   30 REM
   40 REM Mode independent. It draws into whatever screen mode is already
   50 REM current and takes its geometry from the mode variables, so set the
   60 REM mode you want first and then RUN. Nothing here assumes a pixel is
   70 REM four OS units, which is true of MODE 13 but not of MODE 12.
   80 REM
   90 REM What to look for, outermost signal first:
  100 REM
  110 REM   SCREEN BORDER, magenta, outside the picture altogether. If any of
  120 REM   it reaches the TV then the capture window is taking in more than
  130 REM   active video. Note that a mode whose definition gives it no
  140 REM   border area will show none however this is set.
  150 REM
  160 REM   GREEN 1-PIXEL FRAME on the outermost pixels: the exact edge of
  170 REM   the screen. A missing side means that edge is being clipped, and
  180 REM   it is the finest edge signal here. Green because it is the
  190 REM   complement of the magenta border it sits against; red was tried
  200 REM   and is too close to magenta to pick out. It shares its hue with
  210 REM   the left-edge blocks, but a one-pixel line at the extreme edge
  220 REM   is not going to be mistaken for two blocks in the centre.
  230 REM
  240 REM   WHITE BANDS B% pixels thick, B% being a 32nd of the screen
  250 REM   height. Count them in from an edge to measure how much is lost:
  260 REM     3  not clipping      2  lost 1 to 3 bands
  270 REM     1  lost 3 to 5       0  lost more than 5
  280 REM   B% is printed in the centre so a photograph carries its scale.
  290 REM
  300 REM   COLOURED BLOCKS name the edges by count and by hue, so a tilted
  310 REM   photograph is still unambiguous:
  320 REM     1 red bottom   2 green left   3 blue top   4 yellow right
  330 REM
  340 REM   GRATINGS, one pixel on and one pixel off. The vertical lines,
  350 REM   below the circle, test the horizontal sample clock: even, cleanly
  360 REM   separated lines mean PLLAD_MD is right, while moire, beating or
  370 REM   a flat grey wash means it is not. The horizontal lines, above the
  380 REM   circle, do the same for the vertical path. Bypass the input
  390 REM   low-pass filter first, IF_HS_TAP11_BYPS and IF_HS_INT_LPF_BYPS
  400 REM   both 1, or the filter smears a one-pixel line and the grating
  410 REM   says nothing.
  420 REM
  430 REM   CIRCLE IN A SQUARE at the centre, for aspect ratio. Both are
  440 REM   drawn with equal width and height in OS units, so the circle
  450 REM   touches the square at the four midpoints. Judge the square: it is
  460 REM   far easier to see that a square has gone oblong than that a
  470 REM   circle has gone oval, and the two distort by the same factor.
  471 REM
  472 REM   ANIMATION. The screen border and the outermost ring flip colour
  473 REM   twice a second. Anything that flips is being written by the input
  474 REM   formatter right now; anything frozen is scratch space the scaler
  475 REM   is no longer writing, so leftover junk beside a trimmed picture
  476 REM   tells itself apart from live captured border at a glance. Film
  477 REM   it rather than photographing it. The outermost band alternates
  478 REM   white and yellow, so count it as a band in either phase.
  480 REM
  490 REM Colours go through ColourTrans, which picks the nearest entry in
  500 REM whatever palette the mode has. Raw GCOL numbers are not portable:
  510 REM GCOL 3 is yellow in a 16-colour mode and a dark red in a 256-colour
  520 REM one, which is why the earlier card's patches came out alike.
  530 REM
  540 REM Only RECTANGLE FILL is used, the circle included: it is drawn as a
  550 REM stack of one-pixel horizontal runs. On this machine, 2026-08-02,
  560 REM the fine card's MOVE/DRAW features did not appear while its
  570 REM RECTANGLE FILL patches did.
  580 REM
  590 REM FileCore names are 10 characters maximum, hence TestPat. Drop it
  600 REM into hostfs/Xfer and drag it across. Escape exits and puts the
  610 REM screen border back to black.
  620 :
  630 ON ERROR VDU 19,0,24,0,0,0 : ON : PRINT REPORT$;" at line ";ERL : END
  640 :
  650 OFF
  660 SYS "OS_ReadModeVariable",-1,11 TO ,,XW%
  670 SYS "OS_ReadModeVariable",-1,12 TO ,,YW%
  680 SYS "OS_ReadModeVariable",-1,4 TO ,,XE%
  690 SYS "OS_ReadModeVariable",-1,5 TO ,,YE%
  700 SYS "OS_ReadModeVariable",-1,1 TO ,,TX%
  710 SYS "OS_ReadModeVariable",-1,2 TO ,,TY%
  720 W%=XW%+1 : H%=YW%+1
  730 UX%=1<<XE% : UY%=1<<YE%
  740 B%=H% DIV 32 : IF B%<2 THEN B%=2
  750 S%=B% : IF S%<4 THEN S%=4
  760 CX%=W% DIV 2 : CY%=H% DIV 2
  770 :
  780 REM ---- screen border, outside the picture entirely -----------------
  790 VDU 19,0,24,255,0,255
  800 :
  810 REM ---- concentric bands, outermost first ---------------------------
  820 FOR I%=0 TO 5
  830   IF I% AND 1 THEN PROCcol(&00000000) ELSE PROCcol(&FFFFFF00)
  840   PROCpix(I%*B%,I%*B%,W%-2*I%*B%,H%-2*I%*B%)
  850 NEXT
  860 :
  870 REM ---- edge identity, inside the centre field ----------------------
  880 PROCcol(&0000FF00)
  890 PROCblocks(CX%-S% DIV 2, 6*B%, 1, TRUE)
  900 PROCcol(&00FF0000)
  910 PROCblocks(6*B%, CY%-S%, 2, FALSE)
  920 PROCcol(&FF000000)
  930 PROCblocks(CX%-2*S%, H%-6*B%-S%, 3, TRUE)
  940 PROCcol(&00FFFF00)
  950 PROCblocks(W%-6*B%-S%, CY%-3*S%, 4, FALSE)
  960 :
  970 REM ---- gratings, clear of the circle above and below ---------------
  980 GX%=8*B% : GW%=W%-16*B%
  990 IF GW%>7 THEN PROCgrating
 1000 :
 1010 REM ---- circle inscribed in a square, for aspect --------------------
 1020 R%=3*B%*UY%
 1030 PROCcol(&FFFFFF00)
 1040 PROCdisc(CX%,CY%,R%)
 1050 PROCcol(&FFFF0000)
 1060 PROCsquare(CX%,CY%,R%)
 1070 :
 1080 REM ---- scale, so the photograph is self-documenting ----------------
 1090 REM Through ColourTrans for the same reason as the graphics colours:
 1100 REM COLOUR 7 is white in a 16-colour mode but came out red on white
 1110 REM in a 256-colour one. R3 bit 7 set selects the background.
 1120 SYS "ColourTrans_SetTextColour",&FFFFFF00,0,0,0
 1130 SYS "ColourTrans_SetTextColour",&00000000,0,0,128
 1140 CH%=H% DIV (TY%+1)
 1150 PRINT TAB(TX% DIV 2-5, (H%-(CY%-4*B%)) DIV CH%);"BAND ";B%
 1160 :
 1170 REM ---- green frame on the outermost pixels, drawn last -------------
 1180 PROCframe
 1230 :
 1240 PROCanimate
 1250 END
 1260 :
 1270 REM White lines on the black centre field, so the gaps need no
 1280 REM drawing. Horizontal above the circle, vertical below it.
 1290 DEF PROCgrating
 1300 LOCAL I%
 1310 PROCcol(&FFFFFF00)
 1320 FOR I%=0 TO 2*B%-1 STEP 2
 1330   PROCpix(GX%, CY%-8*B%+I%, GW%, 1)
 1340 NEXT
 1350 FOR I%=0 TO GW%-1 STEP 2
 1360   PROCpix(GX%+I%, CY%+4*B%, 1, 2*B%)
 1370 NEXT
 1380 ENDPROC
 1390 :
 1400 REM A filled circle of radius r% OS UNITS, as horizontal runs. Working
 1410 REM in OS units rather than pixels is what makes it a true circle in
 1420 REM modes whose pixels are not square, MODE 12 among them.
 1430 DEF PROCdisc(cx%,cy%,r%)
 1440 LOCAL I%,n%,dy,dx,p%
 1450 n%=r% DIV UY%
 1460 FOR I%=-n% TO n%
 1470   dy=I%*UY%
 1480   dx=SQR(r%*r%-dy*dy)/UX%
 1490   p%=INT(dx)
 1500   PROCpix(cx%-p%,cy%+I%,2*p%+1,1)
 1510 NEXT
 1520 ENDPROC
 1530 :
 1540 REM The square the circle is inscribed in: same r% in OS units, so its
 1550 REM sides are equal in OS units too.
 1560 DEF PROCsquare(cx%,cy%,r%)
 1570 LOCAL hw%,hh%
 1580 hw%=r% DIV UX% : hh%=r% DIV UY%
 1590 PROCpix(cx%-hw%,cy%-hh%,2*hw%+1,1)
 1600 PROCpix(cx%-hw%,cy%+hh%,2*hw%+1,1)
 1610 PROCpix(cx%-hw%,cy%-hh%,1,2*hh%+1)
 1620 PROCpix(cx%+hw%,cy%-hh%,1,2*hh%+1)
 1630 ENDPROC
 1640 :
 1650 REM A filled rectangle pw% x ph% pixels with its corner at px%,py%.
 1660 REM The -1 keeps the fill inside the last pixel rather than spilling
 1670 REM into the next one, since RECTANGLE FILL is inclusive of both ends.
 1680 DEF PROCpix(px%,py%,pw%,ph%)
 1690 RECTANGLE FILL px%*UX%,py%*UY%,pw%*UX%-1,ph%*UY%-1
 1700 ENDPROC
 1710 :
 1720 REM n% blocks of S% pixels from (x%,y%); h% TRUE lays them along X.
 1730 DEF PROCblocks(x%,y%,n%,h%)
 1740 LOCAL I%
 1750 FOR I%=0 TO n%-1
 1760   IF h% THEN PROCpix(x%+I%*2*S%,y%,S%,S%) ELSE PROCpix(x%,y%+I%*2*S%,S%,S%)
 1770 NEXT
 1780 ENDPROC
 1790 :
 1800 REM Nearest palette entry to a &BBGGRR00 colour, in any mode.
 1810 DEF PROCcol(c%)
 1820 SYS "ColourTrans_SetGCOL",c%,0,0,0,0
 1830 ENDPROC
 1840 :
 1850 REM Liveness test. The screen border and the outermost ring flip
 1860 REM colour twice a second, so in a video anything that flips is being
 1870 REM written this frame and anything frozen is scratch space the scaler
 1880 REM has stopped writing to. That tells leftover junk from live
 1890 REM captured border apart without having to reason about window sizes.
 1900 DEF PROCanimate
 1910 LOCAL p%,T%
 1920 p%=0
 1930 REPEAT
 1940   IF p% THEN PROCcol(&00FFFF00) ELSE PROCcol(&FFFFFF00)
 1950   PROCring(B%)
 1960   PROCframe
 1970   IF p% THEN VDU 19,0,24,0,255,255 ELSE VDU 19,0,24,255,0,255
 1980   p%=p% EOR 1
 1990   T%=TIME+50
 2000   REPEAT UNTIL TIME>T%
 2010 UNTIL FALSE
 2020 ENDPROC
 2030 :
 2040 REM The outermost band as four strips, so redrawing it does not wipe
 2050 REM the centre field the way a nested fill would.
 2060 DEF PROCring(t%)
 2070 PROCpix(0,0,W%,t%)
 2080 PROCpix(0,H%-t%,W%,t%)
 2090 PROCpix(0,0,t%,H%)
 2100 PROCpix(W%-t%,0,t%,H%)
 2110 ENDPROC
 2120 :
 2130 REM Redrawn after the ring, which would otherwise cover it.
 2140 DEF PROCframe
 2150 PROCcol(&00FF0000)
 2160 PROCpix(0,0,W%,1)
 2170 PROCpix(0,H%-1,W%,1)
 2180 PROCpix(0,0,1,H%)
 2190 PROCpix(W%-1,0,1,H%)
 2200 ENDPROC
