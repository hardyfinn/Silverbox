# Credits

- **Open303** by Robin Schmidt (MIT licence) was read for the general structure of a 303 model: the coupled diode-ladder form and the critical feedback of about 17. No code was copied.
- **Robin Whittle's Devil Fish** documentation is the source of the SBX control list.
- **Oversampler and polyBLEP** come from the Serge plugin in this project (hardyfinn/Serge, GPL-3.0-or-later).
- **Scala file formats** (`.scl`, `.kbm`): Manuel Op de Coul, Huygens-Fokker Foundation, https://www.huygens-fokker.org/scala/scl_format.html and https://www.huygens-fokker.org/scala/help.htm#mappings
- **Built-in scales:** 5-limit just intonation, Pythagorean and the harmonic series are computed from their ratios. Quarter-comma meantone is computed from fifths of 696.578 cents. Werckmeister III uses the cent values of the Scala archive's `werck3.scl`. Equal temperaments, Bohlen-Pierce (13 equal steps of 3/1) and Wendy Carlos alpha (78.0-cent steps) are generated. Slendro is approximated as 5-tone equal. Pelog is Bill Alves's just-intonation pelog (*1/1* vol. 9 no. 4, 1997; `alves_pelog.scl` in the Scala archive), see https://graphs.unequaltemperament.com/scl_detail.php?scl_id=159
- **303 calibration** (round 2): the TM3 cutoff calibration (500 Hz ± 100 Hz) and the TM4 and TM5 trimmers come from the service notes as quoted in the x0xb0x and RE-303 build pages: https://www.ladyada.net/make/x0xb0x/fab/vcf/index.html and https://smakela.github.io/re-303-buildguide/docs/build_calibration
- **Panel lettering:** DejaVu Sans Bold (Bitstream Vera licence), converted to paths.
- **Display font:** Share Tech Mono, which ships with VCV Rack.

"Roland", "TB-303", "Bass Line" and "Devil Fish" are trademarks of their owners. Silverbox is an independent model and is not affiliated with them.
