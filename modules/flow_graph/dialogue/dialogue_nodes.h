/**************************************************************************/
/*  dialogue_nodes.h                                                      */
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

#pragma once

#include "../flow_nodes.h"

#include "scene/resources/audio/audio_stream.h"
#include "servers/subtitle/subtitle.h"

// Dialogue node types. They wait for the game's dialogue UI, which reacts to FlowGraphPlayback.node_waiting and
// continues with advance() (lines) or choose() (choices). The waiting state lives in the playback, so a restored
// FlowGraphState shows the same line or choice again without reconnecting anything.

// One answer of a DialogueChoiceNode: text, flow output and an optional condition.
class DialogueOption : public Resource {
	GDCLASS(DialogueOption, Resource);

	StringName port;
	String text;
	String condition;
	bool once = false;

	// Parsed once per set of blackboard variable names, dropped when the condition changes.
	mutable Ref<Expression> parsed;
	mutable PackedStringArray parsed_names;

protected:
	static void _bind_methods();

public:
	void set_port(const StringName &p_port);
	StringName get_port() const { return port; }
	void set_text(const String &p_text);
	String get_text() const { return text; }
	void set_condition(const String &p_condition);
	String get_condition() const { return condition; }
	void set_once(bool p_once);
	bool is_once() const { return once; }

	// True if the condition is empty or evaluates to true. Every variable of the blackboard is an input by its name.
	bool is_condition_met(const Ref<FlowBlackboard> &p_blackboard) const;
};

// A line of dialogue: who speaks and what. Waits until the UI calls FlowGraphPlayback.advance().
class DialogueLineNode : public FlowNode {
	GDCLASS(DialogueLineNode, FlowNode);

	StringName speaker;
	String speaker_name;
	String text;
	StringName translation_context;
	Ref<AudioStream> voice;
	Ref<Subtitle> subtitle;

protected:
	void _validate_property(PropertyInfo &p_property) const;
	static void _bind_methods();
	TypedArray<FlowPort> _create_input_ports() const override;
	TypedArray<FlowPort> _create_output_ports() const override;
	String _get_default_category() const override { return "Dialogue"; }
	PackedStringArray _get_default_inline_properties() const override;
	PackedStringArray _get_default_configuration_warnings() const override;
	TypedArray<PackedStringArray> _get_default_translatable_strings() const override;
	FlowGraphPlayback::EnterResult _default_enter(FlowGraphPlayback *p_playback) const override { return FlowGraphPlayback::ENTER_RESULT_WAIT; }

public:
	void set_speaker(const StringName &p_speaker);
	StringName get_speaker() const { return speaker; }
	void set_speaker_name(const String &p_speaker_name);
	String get_speaker_name() const { return speaker_name; }
	void set_text(const String &p_text);
	String get_text() const { return text; }
	void set_translation_context(const StringName &p_context);
	StringName get_translation_context() const { return translation_context; }
	void set_voice(const Ref<AudioStream> &p_voice);
	Ref<AudioStream> get_voice() const { return voice; }
	void set_subtitle(const Ref<Subtitle> &p_subtitle);
	Ref<Subtitle> get_subtitle() const { return subtitle; }

	String get_translated_text() const;
	String get_translated_speaker_name() const;
};

// A choice between options. One flow output per option; waits until the UI calls FlowGraphPlayback.choose().
class DialogueChoiceNode : public FlowNode {
	GDCLASS(DialogueChoiceNode, FlowNode);

	Vector<Ref<DialogueOption>> options;
	StringName translation_context;

	void _options_changed();
	int _find_option(const StringName &p_port) const;

protected:
	static void _bind_methods();
	TypedArray<FlowPort> _create_input_ports() const override;
	TypedArray<FlowPort> _create_output_ports() const override;
	String _get_default_category() const override { return "Dialogue"; }
	PackedStringArray _get_default_configuration_warnings() const override;
	TypedArray<PackedStringArray> _get_default_translatable_strings() const override;
	FlowGraphPlayback::EnterResult _default_enter(FlowGraphPlayback *p_playback) const override { return FlowGraphPlayback::ENTER_RESULT_WAIT; }
	StringName _default_choose_exit(FlowGraphPlayback *p_playback) const override;
	TypedArray<FlowExit> _default_get_available_exits(FlowGraphPlayback *p_playback) const override;
	void _default_exit(FlowGraphPlayback *p_playback, const StringName &p_port) const override;

public:
	void set_options(const TypedArray<DialogueOption> &p_options);
	TypedArray<DialogueOption> get_options() const;
	void set_translation_context(const StringName &p_context);
	StringName get_translation_context() const { return translation_context; }

	StringName get_option_port(int p_index) const;
};
