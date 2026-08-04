   10 REM > ModeSweep
   20 REM Cycle the RISC PC through the stock AKF50 modes, 15.6 - 37.9 kHz, so
   30 REM the far end can watch HPERIOD_IF across real source mode changes.
   40 REM
   50 REM Copy to the RISC PC, then:   *EXEC ModeSweep   and   RUN
   60 REM Escape stops and returns to MODE 12.
   70 :
   80 REM Every mode here has a DISTINCT VTOTAL, so hperiod_sweep.py can tell
   90 REM which one is on air from STATUS_SYNC_PROC_VTOTAL alone. That means the
  100 REM two machines need no clock sync and no agreement about ordering --
  110 REM change this list freely, but keep the VTOTALs distinct or the far end
  120 REM cannot label its samples. That matters more now the visit order
  125 REM rotates per pass (see line 359): the far end cannot assume ordering.
  130 :
  140 REM Modes are from the STOCK Acorn AKF50 mode file, not from a
  150 REM hand-authored one. AKF50 spans 15.625 - 37.879 kHz and has no
  160 REM 1024x768 or 1280x1024 -- if you want those, say which stock file
  170 REM supplies them rather than reaching for a tuned mode file.
  171 REM
  172 REM These ten are ALL TEN distinct VTOTALs stock AKF50 offers: 262, 312,
  173 REM 364, 449, 500, 520, 525, 534, 625, 628. Its other 18 modes duplicate
  174 REM those VTOTALs at different pixel clocks, so adding them buys no extra
  175 REM coverage and breaks the far end's ability to label samples. Verify
  176 REM with mdf_modes.py before changing the list.
  177 REM
  178 REM AKF50 ships in the RPCEmu universal !Boot, under
  179 REM   hostfs/!Boot/Resources/Configure/Monitors/Acorn/AKF50
  180 :
  190 REM A mode missing from the file in use raises "Screen mode not
  200 REM available". That is trapped per mode rather than fatal, so one
  210 REM missing mode does not end the sweep.
  220 :
  230 DWELL%=20
  240 REM seconds per mode. The scaler can take ~6 s to settle after a preset
  250 REM apply, so anything much shorter samples the transient, not the mode.
  252 REM 20 leaves ~14 s of settled signal after that, which is what lets the
  253 REM far end tell "railed and stayed railed" from a blip during the change.
  254 REM HPERIOD_IF has been seen railed for 9 s straight, so a short dwell
  255 REM cannot distinguish the fault from the transition that triggers it.
  260 :
  270 DIM mode$(9)
  280 mode$(0)="X320 Y250 C256 F50" : REM VTOTAL 312, 15.625 kHz, predict 432
  290 mode$(1)="X640 Y200 C256 F60" : REM VTOTAL 262, 15.686 kHz, predict 430
  300 mode$(2)="X640 Y352 C256 F60" : REM VTOTAL 364, 21.853 kHz, predict 309
  310 mode$(3)="X640 Y512 C256 F50" : REM VTOTAL 534, 26.786 kHz, predict 252
  320 mode$(4)="X240 Y352 C256 F70" : REM VTOTAL 449, 31.467 kHz, predict 215
  330 mode$(5)="X320 Y480 C256 F60" : REM VTOTAL 525, 31.468 kHz, predict 215
  340 mode$(6)="X320 Y480 C256 F75" : REM VTOTAL 500, 37.500 kHz, predict 180
  350 mode$(7)="X320 Y480 C256 F73" : REM VTOTAL 520, 37.861 kHz, predict 178
  352 REM The two 800x600 modes are LAST on purpose: both are over 535 lines, so
  353 REM they drop the scaler into RGBHV bypass, where the IF is out of the path
  354 REM and HPERIOD_IF garbage is expected rather than a fault. Set N%=8 to stay
  355 REM below the gate and keep every sample in a real scaling preset.
  356 mode$(8)="X800 Y600 C256 F56" : REM VTOTAL 625, 35.156 kHz  -- OVER THE GATE
  357 mode$(9)="X800 Y600 C256 F60" : REM VTOTAL 628, 37.879 kHz  -- OVER THE GATE
  358 :
  359 REM Visit the modes in a different order each pass. A fixed cycle only ever
  360 REM exercises the same few adjacent transitions, and what rails HPERIOD_IF
  361 REM is a TRANSITION, not a mode -- every mode here reads healthy when the
  362 REM firmware is frozen. Stepping by a value coprime with N% walks all the
  363 REM modes in a different order, so four such steps give four distinct sets
  364 REM of adjacent pairs: 32 of the 56 ordered pairs instead of 8.
  365 N%=8
  366 REM 8 stays under the 535-line bypass gate; 10 adds the two 800x600 modes.
  367 REM Steps must be coprime with N% or the walk revisits a subset.
  368 DIM step%(3)
  369 IF N%=8 THEN step%(0)=1:step%(1)=3:step%(2)=5:step%(3)=7 ELSE step%(0)=1:step%(1)=3:step%(2)=7:step%(3)=9
  370 pass%=0
  375 ON ERROR PROCfailed : GOTO 480
  380 REPEAT
  390   pass%=pass%+1
  395   s%=step%((pass%-1) MOD 4)
  400   FOR j%=0 TO N%-1
  405     i%=(j%*s%) MOD N%
  410     bad%=FALSE
  420     ON ERROR LOCAL bad%=TRUE
  430     MODE mode$(i%)
  440     ON ERROR PROCfailed : GOTO 480
  450     IF bad% THEN PROCskip(mode$(i%)) ELSE PROCshow(mode$(i%),pass%,i%)
  460   NEXT j%
  470 UNTIL FALSE
  480 END
  490 :
  500 DEF PROCshow(m$,p%,i%)
  510 OFF
  520 PRINT'"ModeSweep  pass ";p%;"  step ";s%;"  mode ";i%
  530 PRINT"  ";m$
  540 PRINT'"Escape to stop."
  550 REM Draw something with edges in it: a flat field tells you nothing about
  560 REM whether the capture window is finding the picture.
  570 GCOL 0,255
  580 RECTANGLE 0,0,1279,1023
  590 t%=TIME
  600 REPEAT UNTIL TIME-t%>DWELL%*100
  610 ENDPROC
  620 :
  630 DEF PROCskip(m$)
  640 REM Not available in this mode file. Say so on the next mode that works
  650 REM rather than trying to print in a mode that failed to select.
  660 PRINT'"skipped (not in mode file): ";m$
  670 ENDPROC
  680 :
  690 DEF PROCfailed
  700 MODE 12
  710 IF ERR<>17 THEN PRINT"Error ";ERR;": ";REPORT$
  720 ENDPROC
