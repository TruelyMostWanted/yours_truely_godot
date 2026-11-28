/**************************************************************************/
/*  test_subtitles_dock.cpp                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_subtitles_dock)

#ifdef TOOLS_ENABLED

#include "editor/audio/subtitles_dock.h"

namespace TestSubtitlesDock {

TEST_CASE("[SubtitlesDock] Silence edges for snapping") {
	// 10 ms windows: silence, speech from 0.03 s to 0.07 s, silence.
	const Vector<float> levels = { 0.0f, 0.01f, 0.0f, 0.5f, 0.8f, 0.6f, 0.4f, 0.0f, 0.02f, 0.0f };
	double time = -1.0;
	// Speech starts at 0.03 s, stops at 0.07 s.
	CHECK(SubtitlesDock::find_silence_edge(levels, 0.01, 0.045, 0.02, true, 0.1f, time));
	CHECK(time == doctest::Approx(0.03));
	CHECK(SubtitlesDock::find_silence_edge(levels, 0.01, 0.055, 0.02, false, 0.1f, time));
	CHECK(time == doctest::Approx(0.07));
	// Exactly on the edge.
	CHECK(SubtitlesDock::find_silence_edge(levels, 0.01, 0.03, 0.001, true, 0.1f, time));
	CHECK(time == doctest::Approx(0.03));
	// Out of reach, or the wrong direction within reach.
	CHECK_FALSE(SubtitlesDock::find_silence_edge(levels, 0.01, 0.0, 0.015, true, 0.1f, time));
	CHECK_FALSE(SubtitlesDock::find_silence_edge(levels, 0.01, 0.02, 0.015, false, 0.1f, time));
	// Below the threshold, the small bumps are no speech.
	CHECK_FALSE(SubtitlesDock::find_silence_edge(levels, 0.01, 0.085, 0.01, true, 0.1f, time));
	CHECK_FALSE(SubtitlesDock::find_silence_edge(Vector<float>(), 0.01, 1.0, 1.0, true, 0.1f, time));
}

TEST_CASE("[SubtitlesDock] Signed times for Shift All Segments") {
	double seconds = 0.0;
	CHECK(SubtitlesDock::parse_signed_time("1.5", seconds));
	CHECK(seconds == 1.5);
	CHECK(SubtitlesDock::parse_signed_time("-1.5", seconds));
	CHECK(seconds == -1.5);
	CHECK(SubtitlesDock::parse_signed_time(" - 0:01.250 ", seconds));
	CHECK(seconds == doctest::Approx(-1.25));
	CHECK(SubtitlesDock::parse_signed_time("+1:00:00.000", seconds));
	CHECK(seconds == 3600.0);
	CHECK_FALSE(SubtitlesDock::parse_signed_time("-", seconds));
	CHECK_FALSE(SubtitlesDock::parse_signed_time("soon", seconds));
}

} // namespace TestSubtitlesDock

#endif // TOOLS_ENABLED
