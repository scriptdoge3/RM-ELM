# The control room: research notes

The GUI boards were redrawn from period sources: two human-factors reviews of
US control rooms built in the 1970s, the two American sodium-cooled plants of
the time, and the GE training manual for BWR rod control. This page lists what
came from where, and where the game departs from it.

## Sources

1. T. B. Malone et al. (Essex Corporation), *Human Factors Evaluation of
   Control Room Design and Operator Performance at Three Mile Island-2*,
   NUREG/CR-1270 Vol. 1, US NRC, January 1980.
   <https://www.osti.gov/servlets/purl/5603680>
2. L. R. Peterson, J. Preston-Smith, J. W. Savage, W. F. Rousseau (LLNL),
   *Draft Audit Report: Human-Factors Engineering Control-Room Design Review,
   Shoreham Nuclear Power Station*, UCID-19122, April 1981.
   <https://www.osti.gov/servlets/purl/5639979>
3. Hanford Engineering Development Laboratory, *A Summary Description of the
   Fast Flux Test Facility*, HEDL-400, 1980.
   <https://www.osti.gov/servlets/purl/6032523>
4. Project Management Corporation, *Clinch River Breeder Reactor Project
   Preliminary Safety Analysis Report, Summary Volume*, 1975.
   <https://www.nrc.gov/docs/ML0829/ML082960481.pdf>
5. USNRC Technical Training Center, *GE Systems Technology Manual*, Chapter 7.1
   Reactor Manual Control System.
   <https://www.nrc.gov/docs/ML1125/ML11258A342.pdf>
6. ISA-S18.1, *Annunciator Sequences and Specifications* (1979), for the
   ringback sequence.

## What the boards take from them

**One board per plant area, one section per system.** The FFTF control room
(HEDL-400 section 6.2.5 and figure 6-8) puts reactor controls, heat transport
controls, the plant control system, electrical power and emergency decay heat
removal on their own panels round the operators' console, with the panels and
annunciators along the walls. The game's six boards follow that split:
reactor, heat transport, turbine-generator, electrical, auxiliary, and the
remote shutdown panel.

**Annunciators grouped over their system, coloured by priority.** The TMI-2
review found "annunciators (750 total) which are not functionally grouped nor
prioritized" and "poorly organized, are not color coded". The fix the reviews
point to is what the boards do: a window box on the hood over each system
section, windows backlit red (trip), amber (alarm) or white (status), spare
windows left blank, and row letters and column numbers so a window can be
called out ("2-2 A2"). The Shoreham audit flags "red tiles used for other than
first-outs"; red is kept for trips and for the reactor trip first-out box.

**A horn per board.** TMI-2: "auditory displays associated with annunciators
lack directional properties". Each board's horn has its own pitch, and its
button in the header flashes, so the operator can tell which board is calling.

**Demarcation and labels.** Shoreham lists "multiple system panel with no tape
demarcation" and "temporary labeling on J-handles". The benchboards have black
demarcation tape round each group of controls and engraved black nameplates
for systems and components; blue label tape marks the notes operators added
themselves.

**Mimics.** TMI-2 wants controls "laid out in a sequential or otherwise
logical fashion (i.e., mimic)". The switches sit on painted flow lines in the
colour of the fluid, with arrows: the primary loops through the IHXs and
pumps, the secondary loops to the steam generators, the feed train, the steam
path, the cover gas system, the one-line diagram.

**Switches.** The GE SBM J-handle is "used throughout nuclear industry"
(NUREG/CR-1270); breakers get J-handles, pumps and valves pistol grips, each on
a square escutcheon with its red and green indicating lights and a target flag
showing the last operation. Shoreham faults J-handles set "too close to the
edge of the benchboard"; the switches here sit back from the front lip.

**No mirror imaging.** Shoreham notes that "the IRM range switches are mirror
imaged". The IRM range switches on 1-1 run A to H left to right.

**Decay heat removal stations.** FFTF: "manual control of the fine damper is
provided from the control room for each of the 12 modules; the DHX outlet
temperature for each module is displayed adjacent to the manual controller."
Section 2-3 has a damper loading station per DRACS train with a dial above
it. The game shows each train's heat rather than its outlet temperature,
because the model keeps one mixed DRACS return stream.

**Emergency shutdown outside the control room.** FFTF provides "emergency
controls for reactor and HTS prime mover shutdown, containment isolation and
decay heat removal ... at remote locations". The remote shutdown panel carries
the reactor scram, primary pumps and pony motors, DRACS dampers, diesels, SG
isolation, feed and turbine trip.

**Steam generator protection.** The Clinch River PSAR has leak detection
systems and the sodium-water reaction pressure relief subsystem (SWRPRS)
instrumentation and controls as a separate protection function. Section 2-2
gathers the hydrogen-in-sodium meters and recorder, a leak detection display
per steam generator, and the guarded SG ISOLATE and LOOP DUMP pushbuttons.

**Rod control.** The GE Reactor Manual Control System puts the rod select
module "in the center of the reactor control panel": an array of
pushbuttons, one per rod, with INSERT, WITHDRAW, CONTINUOUS INSERT and
CONTINUOUS WITHDRAW pushbuttons, and a settle period after each notch while
the rod comes to rest on its collet. Section 1-2 has the rod select matrix,
those pushbuttons and lamps, the rod block lamps (withdrawal block, rod worth
minimizer, rod block monitor), rod drift with test and reset, and the full
core display above.

**Annunciator sequence.** ISA-S18.1 ringback: an alarm flashes fast and
sounds the horn; SILENCE quiets the horn; ACKNOWLEDGE makes it steady; when
the condition clears the window flashes slowly until RESET. TEST lights every
window. The response pushbuttons run along the front lip of every board.

## Where the game departs

- RM-ELM is a sodium-cooled plant with BWR-style neutron monitoring and rod
  control (SRM/IRM/APRM, the mode switch, rod groups). That mix is the game's
  own; neither FFTF nor Clinch River had a BWR rod control system.
- Controllers are drawn as Bailey-style M/A stations (setpoint index,
  process pointer, output meter, A and M buttons). The pump speed, feed,
  voltage regulator and flow master stations are GUI-side controllers on
  top of the plant model.
- Everything fits on one 1920x1080 screen per board, so the sections are
  far denser than a real benchboard, and some period details (switch
  escutcheon legends, meter scales) are simplified to stay legible at that
  size.
