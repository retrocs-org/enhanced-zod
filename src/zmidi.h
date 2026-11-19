#ifndef _ZMIDI_H_
#define _ZMIDI_H_

// Minimal Standard MIDI File reader/slicer for the music engine.
//
// The original Z soundtrack ships as MIDI (assets/sounds/*.MID). The engine's
// dynamic music jumps between sections of a planet's track as the danger level
// changes, but seeking inside a MIDI stream is very slow with a software
// synthesizer (everything up to the target has to be rendered). Instead, the
// music engine asks this class for a new, self-contained MIDI file that starts
// at the wanted time: the tempo, instruments, controllers and pitch bends in
// force at that point are written first, followed by the rest of the song.
// Playing that slice starts instantly.

#include <string>
#include <vector>

class ZMidiFile
{
public:
	ZMidiFile() : division(0), length_seconds(0) {}

	bool Load(const std::string &filename);
	bool Loaded() const { return !events.empty(); }
	double Length() const { return length_seconds; }

	// A type-0 MIDI file that begins at start_seconds (clamped to the song).
	std::vector<unsigned char> Slice(double start_seconds) const;

private:
	struct Event
	{
		unsigned int tick;
		double seconds;
		std::vector<unsigned char> data;   //status byte + data, complete (no running status)
	};

	int division;                 //ticks per quarter note
	double length_seconds;
	std::vector<Event> events;    //all tracks merged, in time order
};

#endif
