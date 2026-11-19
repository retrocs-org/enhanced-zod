SoundFonts for the original MIDI soundtrack
===========================================

The 1996 score (assets/sounds/*.MID) is played through FluidSynth, which needs
a General MIDI SoundFont. CMake downloads GeneralUser GS into this folder when
you configure the build. To use another one, put a single .sf2 (or .sf3) file
here, or point the ZOD_SOUNDFONT environment variable at one.

With no SoundFont present the game plays its recorded tracks instead.
