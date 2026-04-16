#ifndef _RIVEEXTENSION_API_SCENE_HPP_
#define _RIVEEXTENSION_API_SCENE_HPP_

// godot-cpp
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/vector2.hpp>

// rive-runtime
#include <rive/animation/state_machine_bool.hpp>
#include <rive/animation/state_machine_input_instance.hpp>
#include <rive/animation/state_machine_instance.hpp>
#include <rive/animation/state_machine_number.hpp>
#include <rive/scene.hpp>

// extension
#include "api/instances.hpp"
#include "api/rive_input.hpp"
#include "api/rive_listener.hpp"
#include "rive_exceptions.hpp"

using namespace godot;

class RiveScene : public Resource {
    GDCLASS(RiveScene, Resource);

    friend class RiveArtboard;
    friend class RiveInstance;
    friend class RiveViewerBase;

   private:
    rive::ArtboardInstance *artboard;
    Ptr<rive::StateMachineInstance> scene;
    int index = -1;
    String name = "";

    Instances<RiveInput> inputs = Instances<RiveInput>([this](int index) -> Ref<RiveInput> {
        if (!exists() || index < 0 || index >= scene->inputCount()) return nullptr;
        return RiveInput::MakeRef(scene->input(index), index);
    });

    Instances<RiveListener> listeners = Instances<RiveListener>([this](int index) -> Ref<RiveListener> {
        if (!exists() || !scene->stateMachine() || index < 0 || index >= scene->stateMachine()->inputCount())
            return nullptr;
        return RiveListener::MakeRef(scene->stateMachine()->listener(index), index);
    });

   protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("exists"), &RiveScene::exists);
        ClassDB::bind_method(D_METHOD("get_index"), &RiveScene::get_index);
        ClassDB::bind_method(D_METHOD("get_name"), &RiveScene::get_name);
        ClassDB::bind_method(D_METHOD("get_input_count"), &RiveScene::get_input_count);
        ClassDB::bind_method(D_METHOD("get_listener_count"), &RiveScene::get_listener_count);
        ClassDB::bind_method(D_METHOD("get_inputs"), &RiveScene::get_inputs);
        ClassDB::bind_method(D_METHOD("get_listeners"), &RiveScene::get_listeners);
        ClassDB::bind_method(D_METHOD("get_input_names"), &RiveScene::get_input_names);
        ClassDB::bind_method(D_METHOD("get_bounds"), &RiveScene::get_bounds);
        ClassDB::bind_method(D_METHOD("get_duration"), &RiveScene::get_duration);
        ClassDB::bind_method(D_METHOD("is_opaque"), &RiveScene::is_opaque);
        ClassDB::bind_method(D_METHOD("get_input", "index"), &RiveScene::get_input);
        ClassDB::bind_method(D_METHOD("find_input", "name"), &RiveScene::find_input);
        ClassDB::bind_method(D_METHOD("reset_input", "index"), &RiveScene::reset_input);
        ClassDB::bind_method(D_METHOD("get_listener", "index"), &RiveScene::get_listener);
        ClassDB::bind_method(D_METHOD("find_listener", "index"), &RiveScene::find_listener);
        ClassDB::bind_method(D_METHOD("is_loop"), &RiveScene::is_loop);
        ClassDB::bind_method(D_METHOD("is_pingpong"), &RiveScene::is_pingpong);
        ClassDB::bind_method(D_METHOD("is_one_shot"), &RiveScene::is_one_shot);

        // Nested artboard input methods (explicit path + name)
        ClassDB::bind_method(D_METHOD("get_bool_at_path", "name", "path"), &RiveScene::get_bool_at_path);
        ClassDB::bind_method(D_METHOD("set_bool_at_path", "name", "path", "value"), &RiveScene::set_bool_at_path);
        ClassDB::bind_method(D_METHOD("get_number_at_path", "name", "path"), &RiveScene::get_number_at_path);
        ClassDB::bind_method(D_METHOD("set_number_at_path", "name", "path", "value"), &RiveScene::set_number_at_path);
        ClassDB::bind_method(D_METHOD("fire_trigger_at_path", "name", "path"), &RiveScene::fire_trigger_at_path);
        ClassDB::bind_method(D_METHOD("get_input_at_path", "name", "path"), &RiveScene::get_input_at_path);

        // Convenience methods (combined path format: "NestedArtboard/InputName")
        ClassDB::bind_method(D_METHOD("set_input", "input_path", "value"), &RiveScene::set_input);
        ClassDB::bind_method(D_METHOD("get_input_value", "input_path"), &RiveScene::get_input_value);
        ClassDB::bind_method(D_METHOD("fire", "input_path"), &RiveScene::fire);
    }

    void _instantiate_inputs() {
        if (exists())
            for (int i = 0; i < scene->inputCount(); i++) {
                auto input = get_input(i);
                if (input.is_null() || !input->exists()) throw RiveException("Failed to instantiate input.");
            }
    }

    void _get_input_property_list(List<PropertyInfo> *list) const {
        inputs.for_each([list](Ref<RiveInput> input, int _) {
            list->push_back(PropertyInfo(input->get_type(), input->get_name()));
        });
    }

   public:
    static Ref<RiveScene> MakeRef(
        rive::ArtboardInstance *artboard_value,
        Ptr<rive::StateMachineInstance> scene_value,
        int index_value,
        String name_value
    ) {
        if (!artboard_value || !scene_value) return nullptr;
        Ref<RiveScene> obj = memnew(RiveScene);
        obj->artboard = artboard_value;
        obj->scene = std::move(scene_value);
        obj->index = index_value;
        obj->name = name_value;
        return obj;
    }

    RiveScene() {}

    bool exists() const {
        return scene != nullptr;
    }

    int get_index() const {
        return index;
    }

    String get_name() const {
        return name;
    }

    int get_input_count() const {
        // Return actual scene input count, not cache size
        return scene ? static_cast<int>(scene->inputCount()) : 0;
    }

    int get_listener_count() const {
        // Return actual state machine listener count, not cache size
        return (scene && scene->stateMachine()) ? static_cast<int>(scene->stateMachine()->listenerCount()) : 0;
    }

    TypedArray<RiveInput> get_inputs() const {
        return inputs.get_list();
    }

    TypedArray<RiveListener> get_listeners() const {
        return listeners.get_list();
    }

    PackedStringArray get_input_names() const {
        PackedStringArray names;
        inputs.for_each([&names](Ref<RiveInput> input, int _) { names.append(input->get_name()); });
        return names;
    }

    Rect2 get_bounds() const {
        auto aabb = scene ? scene->bounds() : rive::AABB();
        return Rect2(aabb.left(), aabb.top(), aabb.width(), aabb.height());
    }

    float get_duration() const {
        return scene ? scene->durationSeconds() : -1;
    }

    bool is_opaque() const {
        return scene ? !scene->isTranslucent() : true;
    }

    Ref<RiveInput> get_input(int index) {
        return inputs.get(index);
    }

    Ref<RiveInput> find_input(String name) const {
        return inputs.find([name](Ref<RiveInput> input, int index) { return input->get_name() == name; });
    }

    Ref<RiveInput> reset_input(int index) {
        return inputs.reinstantiate(index);
    }

    Ref<RiveListener> get_listener(int index) {
        return listeners.get(index);
    }

    Ref<RiveListener> find_listener(String name) const {
        return listeners.find([name](Ref<RiveListener> listener, int index) { return listener->get_name() == name; });
    }

    bool is_loop() const {
        return scene ? scene->loop() == rive::Loop::loop : false;
    }

    bool is_pingpong() const {
        return scene ? scene->loop() == rive::Loop::pingPong : false;
    }

    bool is_one_shot() const {
        return scene ? scene->loop() == rive::Loop::oneShot : false;
    }

    void move_mouse(rive::Mat2D inverse_transform, Vector2 position) {
        if (scene) scene->pointerMove(inverse_transform * rive::Vec2D(position.x, position.y));
    }

    void press_mouse(rive::Mat2D inverse_transform, Vector2 position) {
        if (scene) scene->pointerDown(inverse_transform * rive::Vec2D(position.x, position.y));
    }

    void release_mouse(rive::Mat2D inverse_transform, Vector2 position) {
        if (scene) scene->pointerUp(inverse_transform * rive::Vec2D(position.x, position.y));
    }

    /* Nested Artboard Input Methods */

    /// Get a boolean input from a nested artboard
    /// @param name The input name (e.g., "isActive")
    /// @param path The path to the nested artboard (e.g., "Button1" or "Panel/Button1")
    Variant get_bool_at_path(const String& name, const String& path) const {
        if (!artboard) return Variant();
        rive::SMIBool* input = artboard->getBool(name.utf8().get_data(), path.utf8().get_data());
        if (input) return input->value();
        return Variant();
    }

    /// Set a boolean input on a nested artboard
    bool set_bool_at_path(const String& name, const String& path, bool value) {
        if (!artboard) return false;
        rive::SMIBool* input = artboard->getBool(name.utf8().get_data(), path.utf8().get_data());
        if (input) {
            input->value(value);
            return true;
        }
        return false;
    }

    /// Get a number input from a nested artboard
    Variant get_number_at_path(const String& name, const String& path) const {
        if (!artboard) return Variant();
        rive::SMINumber* input = artboard->getNumber(name.utf8().get_data(), path.utf8().get_data());
        if (input) return input->value();
        return Variant();
    }

    /// Set a number input on a nested artboard
    bool set_number_at_path(const String& name, const String& path, float value) {
        if (!artboard) return false;
        rive::SMINumber* input = artboard->getNumber(name.utf8().get_data(), path.utf8().get_data());
        if (input) {
            input->value(value);
            return true;
        }
        return false;
    }

    /// Fire a trigger input on a nested artboard
    bool fire_trigger_at_path(const String& name, const String& path) {
        if (!artboard) return false;
        rive::SMITrigger* input = artboard->getTrigger(name.utf8().get_data(), path.utf8().get_data());
        if (input) {
            input->fire();
            return true;
        }
        return false;
    }

    /// Get any input from a nested artboard (returns RiveInput wrapper)
    Ref<RiveInput> get_input_at_path(const String& name, const String& path) {
        if (!artboard) return nullptr;
        rive::SMIInput* input = artboard->input(name.utf8().get_data(), path.utf8().get_data());
        if (input) return RiveInput::MakeRef(input, -1);  // -1 index since it's a path-based lookup
        return nullptr;
    }

    /* Convenience Methods - Combined Path Format */
    // These accept a single string like "NestedArtboard/InputName" or "Parent/Child/InputName"

   private:
    /// Split a combined path like "Cirkle1/Color" into path ("Cirkle1") and name ("Color")
    /// For nested paths like "Panel/Button/isActive", path = "Panel/Button", name = "isActive"
    std::pair<String, String> _split_input_path(const String& full_path) const {
        int last_slash = full_path.rfind("/");
        if (last_slash == -1) {
            // No slash - name only, empty path (root level)
            return std::make_pair(String(), full_path);
        }
        return std::make_pair(full_path.substr(0, last_slash), full_path.substr(last_slash + 1));
    }

   public:
    /// Set an input value using combined path format (e.g., "Cirkle1/Color" for nested, "Color" for root)
    /// @param input_path Combined path like "NestedArtboard/InputName"
    /// @param value The value to set (bool for boolean, float for number)
    /// @returns true if input was found and set
    bool set_input(const String& input_path, const Variant& value) {
        auto [path, name] = _split_input_path(input_path);

        if (path.is_empty()) {
            // Root level input - use the existing find_input method
            Ref<RiveInput> input = find_input(name);
            if (input.is_valid() && input->exists()) {
                input->set_value(value);
                return true;
            }
            return false;
        }

        // Nested input
        if (value.get_type() == Variant::BOOL) {
            return set_bool_at_path(name, path, (bool)value);
        } else if (value.get_type() == Variant::FLOAT || value.get_type() == Variant::INT) {
            return set_number_at_path(name, path, (float)value);
        }
        return false;
    }

    /// Get an input value using combined path format
    Variant get_input_value(const String& input_path) const {
        auto [path, name] = _split_input_path(input_path);

        if (path.is_empty()) {
            // Root level input
            Ref<RiveInput> input = find_input(name);
            if (input.is_valid() && input->exists()) {
                return input->get_value();
            }
            return Variant();
        }

        // Try bool first, then number
        Variant result = get_bool_at_path(name, path);
        if (result.get_type() != Variant::NIL) return result;
        return get_number_at_path(name, path);
    }

    /// Fire a trigger using combined path format (e.g., "Cirkle1/Click" or "Click")
    bool fire(const String& input_path) {
        auto [path, name] = _split_input_path(input_path);

        if (path.is_empty()) {
            // Root level trigger
            Ref<RiveInput> input = find_input(name);
            if (input.is_valid() && input->exists() && input->is_trigger()) {
                input->fire();
                return true;
            }
            return false;
        }

        return fire_trigger_at_path(name, path);
    }

    /* Overrides */

    String _to_string() const {
        Dictionary format_args;
        format_args["cls"] = get_class_static();
        format_args["name"] = get_name();
        return String("{cls}({name})").format(format_args);
    }

    /* Operators */

    bool operator==(const RiveScene &other) const {
        return other.scene == scene;
    }

    bool operator!=(const RiveScene &other) const {
        return other.scene != scene;
    }
};

#endif