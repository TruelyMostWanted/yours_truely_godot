/**************************************************************************/
/*  dialogue_nodes.cpp                                                    */
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

#include "dialogue_nodes.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/templates/hash_set.h"
#include "servers/subtitle/subtitle_server.h"

static TypedArray<FlowPort> _dialogue_flow_port(const StringName &p_name) {
	TypedArray<FlowPort> ports;
	ports.push_back(FlowPort::create_flow(p_name));
	return ports;
}

// DialogueOption.

void DialogueOption::set_port(const StringName &p_port) {
	port = p_port;
	emit_changed();
}

void DialogueOption::set_text(const String &p_text) {
	text = p_text;
	emit_changed();
}

void DialogueOption::set_condition(const String &p_condition) {
	condition = p_condition;
	parsed.unref();
	emit_changed();
}

void DialogueOption::set_once(bool p_once) {
	once = p_once;
	emit_changed();
}

bool DialogueOption::is_condition_met(const Ref<FlowBlackboard> &p_blackboard) const {
	if (condition.strip_edges().is_empty()) {
		return true;
	}
	PackedStringArray names;
	Array inputs;
	if (p_blackboard.is_valid()) {
		const Dictionary variables = p_blackboard->get_variables();
		const Array keys = variables.keys();
		for (int i = 0; i < keys.size(); i++) {
			names.push_back(keys[i]);
			inputs.push_back(variables[keys[i]]);
		}
	}
	if (parsed.is_null() || names != parsed_names) {
		parsed.instantiate();
		parsed_names = names;
		if (parsed->parse(condition, names) != OK) {
			const String error = parsed->get_error_text();
			parsed.unref();
			ERR_FAIL_V_MSG(false, vformat("DialogueOption: can't parse the condition \"%s\": %s", condition, error));
		}
	}
	// No base instance: the condition sees the variables and global functions only.
	const Variant result = parsed->execute(inputs, nullptr, true);
	return !parsed->has_execute_failed() && result.booleanize();
}

void DialogueOption::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_port", "port"), &DialogueOption::set_port);
	ClassDB::bind_method(D_METHOD("get_port"), &DialogueOption::get_port);
	ClassDB::bind_method(D_METHOD("set_text", "text"), &DialogueOption::set_text);
	ClassDB::bind_method(D_METHOD("get_text"), &DialogueOption::get_text);
	ClassDB::bind_method(D_METHOD("set_condition", "condition"), &DialogueOption::set_condition);
	ClassDB::bind_method(D_METHOD("get_condition"), &DialogueOption::get_condition);
	ClassDB::bind_method(D_METHOD("set_once", "once"), &DialogueOption::set_once);
	ClassDB::bind_method(D_METHOD("is_once"), &DialogueOption::is_once);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "port"), "set_port", "get_port");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "text", PROPERTY_HINT_MULTILINE_TEXT), "set_text", "get_text");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "condition", PROPERTY_HINT_EXPRESSION), "set_condition", "get_condition");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "once"), "set_once", "is_once");
}

// DialogueLineNode.

TypedArray<FlowPort> DialogueLineNode::_create_input_ports() const {
	return _dialogue_flow_port(SNAME("in"));
}

TypedArray<FlowPort> DialogueLineNode::_create_output_ports() const {
	return _dialogue_flow_port(SNAME("out"));
}

PackedStringArray DialogueLineNode::_get_default_inline_properties() const {
	return PackedStringArray({ "speaker", "text" });
}

PackedStringArray DialogueLineNode::_get_default_configuration_warnings() const {
	PackedStringArray warnings;
	if (text.strip_edges().is_empty()) {
		warnings.push_back(RTR("The line has no text."));
	}
	return warnings;
}

TypedArray<PackedStringArray> DialogueLineNode::_get_default_translatable_strings() const {
	// Entries: message id, context, plural id, comment.
	TypedArray<PackedStringArray> strings;
	if (!text.is_empty()) {
		strings.push_back(PackedStringArray({ text, translation_context, String(), speaker.is_empty() ? String() : "Speaker: " + String(speaker) }));
	}
	if (!speaker_name.is_empty()) {
		strings.push_back(PackedStringArray({ speaker_name, translation_context, String(), "Speaker name" }));
	}
	return strings;
}

void DialogueLineNode::set_speaker(const StringName &p_speaker) {
	speaker = p_speaker;
	emit_changed();
}

void DialogueLineNode::set_speaker_name(const String &p_speaker_name) {
	speaker_name = p_speaker_name;
	emit_changed();
}

void DialogueLineNode::set_text(const String &p_text) {
	text = p_text;
	emit_changed();
}

void DialogueLineNode::set_translation_context(const StringName &p_context) {
	translation_context = p_context;
	emit_changed();
}

void DialogueLineNode::set_voice(const Ref<AudioStream> &p_voice) {
	voice = p_voice;
	emit_changed();
}

void DialogueLineNode::set_subtitle(const Ref<Subtitle> &p_subtitle) {
	subtitle = p_subtitle;
	emit_changed();
}

String DialogueLineNode::get_translated_text() const {
	return text.is_empty() ? String() : tr(text, translation_context);
}

String DialogueLineNode::get_translated_speaker_name() const {
	if (!speaker_name.is_empty()) {
		return tr(speaker_name, translation_context);
	}
	// The project's speakers (#483): the display name in the subtitles channel.
	const SubtitleServer *server = SubtitleServer::get_singleton();
	const Ref<DialogueSpeaker> known = server != nullptr ? server->get_speakers()->get_speaker(speaker) : Ref<DialogueSpeaker>();
	return known.is_valid() ? known->get_translated_name() : String(speaker);
}

void DialogueLineNode::_validate_property(PropertyInfo &p_property) const {
	if (p_property.name == "speaker") {
		SubtitleServer::add_speaker_id_hint(p_property);
	}
}

void DialogueLineNode::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_speaker", "speaker"), &DialogueLineNode::set_speaker);
	ClassDB::bind_method(D_METHOD("get_speaker"), &DialogueLineNode::get_speaker);
	ClassDB::bind_method(D_METHOD("set_speaker_name", "speaker_name"), &DialogueLineNode::set_speaker_name);
	ClassDB::bind_method(D_METHOD("get_speaker_name"), &DialogueLineNode::get_speaker_name);
	ClassDB::bind_method(D_METHOD("set_text", "text"), &DialogueLineNode::set_text);
	ClassDB::bind_method(D_METHOD("get_text"), &DialogueLineNode::get_text);
	ClassDB::bind_method(D_METHOD("set_translation_context", "context"), &DialogueLineNode::set_translation_context);
	ClassDB::bind_method(D_METHOD("get_translation_context"), &DialogueLineNode::get_translation_context);
	ClassDB::bind_method(D_METHOD("set_voice", "voice"), &DialogueLineNode::set_voice);
	ClassDB::bind_method(D_METHOD("get_voice"), &DialogueLineNode::get_voice);
	ClassDB::bind_method(D_METHOD("set_subtitle", "subtitle"), &DialogueLineNode::set_subtitle);
	ClassDB::bind_method(D_METHOD("get_subtitle"), &DialogueLineNode::get_subtitle);
	ClassDB::bind_method(D_METHOD("get_translated_text"), &DialogueLineNode::get_translated_text);
	ClassDB::bind_method(D_METHOD("get_translated_speaker_name"), &DialogueLineNode::get_translated_speaker_name);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "speaker"), "set_speaker", "get_speaker");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "speaker_name"), "set_speaker_name", "get_speaker_name");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "text", PROPERTY_HINT_MULTILINE_TEXT), "set_text", "get_text");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "translation_context"), "set_translation_context", "get_translation_context");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "voice", PROPERTY_HINT_RESOURCE_TYPE, "AudioStream"), "set_voice", "get_voice");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "subtitle", PROPERTY_HINT_RESOURCE_TYPE, "Subtitle"), "set_subtitle", "get_subtitle");
}

// DialogueChoiceNode.

void DialogueChoiceNode::_options_changed() {
	notify_ports_changed();
}

int DialogueChoiceNode::_find_option(const StringName &p_port) const {
	for (int i = 0; i < options.size(); i++) {
		if (options[i].is_valid() && get_option_port(i) == p_port) {
			return i;
		}
	}
	return -1;
}

StringName DialogueChoiceNode::get_option_port(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, options.size(), StringName());
	const Ref<DialogueOption> &option = options[p_index];
	return (option.is_valid() && !option->get_port().is_empty()) ? option->get_port() : StringName("option_" + itos(p_index));
}

TypedArray<FlowPort> DialogueChoiceNode::_create_input_ports() const {
	return _dialogue_flow_port(SNAME("in"));
}

TypedArray<FlowPort> DialogueChoiceNode::_create_output_ports() const {
	TypedArray<FlowPort> ports;
	for (int i = 0; i < options.size(); i++) {
		if (options[i].is_valid()) {
			const Ref<FlowPort> port = FlowPort::create_flow(get_option_port(i));
			port->set_label(options[i]->get_text());
			ports.push_back(port);
		}
	}
	return ports;
}

PackedStringArray DialogueChoiceNode::_get_default_configuration_warnings() const {
	PackedStringArray warnings;
	bool fallback = false;
	HashSet<StringName> ports;
	for (int i = 0; i < options.size(); i++) {
		if (options[i].is_null()) {
			continue;
		}
		if (ports.has(get_option_port(i))) {
			warnings.push_back(vformat(RTR("Two options use the port '%s'."), get_option_port(i)));
		}
		ports.insert(get_option_port(i));
		fallback = fallback || (options[i]->get_condition().strip_edges().is_empty() && !options[i]->is_once());
	}
	if (ports.is_empty()) {
		warnings.push_back(RTR("The choice has no options."));
	} else if (!fallback) {
		warnings.push_back(RTR("Every option has a condition or is shown once: the dialogue can get stuck when none is available."));
	}
	return warnings;
}

TypedArray<PackedStringArray> DialogueChoiceNode::_get_default_translatable_strings() const {
	TypedArray<PackedStringArray> strings;
	for (const Ref<DialogueOption> &option : options) {
		if (option.is_valid() && !option->get_text().is_empty()) {
			strings.push_back(PackedStringArray({ option->get_text(), translation_context, String(), "Dialogue option" }));
		}
	}
	return strings;
}

StringName DialogueChoiceNode::_default_choose_exit(FlowGraphPlayback *p_playback) const {
	const TypedArray<FlowExit> exits = _default_get_available_exits(p_playback);
	for (int i = 0; i < exits.size(); i++) {
		const Ref<FlowExit> flow_exit = exits[i];
		if (flow_exit->is_enabled()) {
			return flow_exit->get_port();
		}
	}
	return StringName();
}

TypedArray<FlowExit> DialogueChoiceNode::_default_get_available_exits(FlowGraphPlayback *p_playback) const {
	const PackedStringArray taken = p_playback->get_node_memory();
	TypedArray<FlowExit> exits;
	for (int i = 0; i < options.size(); i++) {
		const Ref<DialogueOption> &option = options[i];
		if (option.is_null()) {
			continue;
		}
		Ref<FlowExit> flow_exit;
		flow_exit.instantiate();
		flow_exit->set_port(get_option_port(i));
		flow_exit->set_label(tr(option->get_text(), translation_context));
		flow_exit->set_enabled(!(option->is_once() && taken.has(get_option_port(i))) && option->is_condition_met(p_playback->get_blackboard()));
		exits.push_back(flow_exit);
	}
	return exits;
}

void DialogueChoiceNode::_default_exit(FlowGraphPlayback *p_playback, const StringName &p_port) const {
	const int index = _find_option(p_port);
	if (index >= 0 && options[index]->is_once()) {
		PackedStringArray taken = p_playback->get_node_memory();
		if (!taken.has(p_port)) {
			taken.push_back(p_port);
			p_playback->set_node_memory(taken);
		}
	}
}

void DialogueChoiceNode::set_options(const TypedArray<DialogueOption> &p_options) {
	const Callable callback = callable_mp(this, &DialogueChoiceNode::_options_changed);
	for (const Ref<DialogueOption> &option : options) {
		if (option.is_valid()) {
			option->disconnect_changed(callback);
		}
	}
	options.clear();
	for (int i = 0; i < p_options.size(); i++) {
		const Ref<DialogueOption> option = p_options[i];
		options.push_back(option);
		if (option.is_valid()) {
			option->connect_changed(callback);
		}
	}
	notify_ports_changed();
}

TypedArray<DialogueOption> DialogueChoiceNode::get_options() const {
	TypedArray<DialogueOption> result;
	for (const Ref<DialogueOption> &option : options) {
		result.push_back(option);
	}
	return result;
}

void DialogueChoiceNode::set_translation_context(const StringName &p_context) {
	translation_context = p_context;
	emit_changed();
}

void DialogueChoiceNode::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_options", "options"), &DialogueChoiceNode::set_options);
	ClassDB::bind_method(D_METHOD("get_options"), &DialogueChoiceNode::get_options);
	ClassDB::bind_method(D_METHOD("set_translation_context", "context"), &DialogueChoiceNode::set_translation_context);
	ClassDB::bind_method(D_METHOD("get_translation_context"), &DialogueChoiceNode::get_translation_context);
	ClassDB::bind_method(D_METHOD("get_option_port", "index"), &DialogueChoiceNode::get_option_port);

	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "options", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("DialogueOption")), "set_options", "get_options");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "translation_context"), "set_translation_context", "get_translation_context");
}
