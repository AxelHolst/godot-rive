#ifndef _RIVEEXTENSION_API_EVENT_HPP_
#define _RIVEEXTENSION_API_EVENT_HPP_

// godot-cpp
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

// rive-runtime
#include <rive/event.hpp>
#include <rive/event_report.hpp>
#include <rive/custom_property.hpp>
#include <rive/custom_property_boolean.hpp>
#include <rive/custom_property_number.hpp>
#include <rive/custom_property_string.hpp>

namespace godot {

class RiveEvent : public RefCounted {
    GDCLASS(RiveEvent, RefCounted);

   private:
    String name;
    float seconds_delay = 0.0f;
    Dictionary properties;

    friend class RiveViewerBase;

   protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("get_name"), &RiveEvent::get_name);
        ClassDB::bind_method(D_METHOD("get_seconds_delay"), &RiveEvent::get_seconds_delay);
        ClassDB::bind_method(D_METHOD("get_properties"), &RiveEvent::get_properties);
        ClassDB::bind_method(D_METHOD("get_property", "name"), &RiveEvent::get_property);
        ClassDB::bind_method(D_METHOD("has_property", "name"), &RiveEvent::has_property);

        ADD_PROPERTY(PropertyInfo(Variant::STRING, "name"), "", "get_name");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "seconds_delay"), "", "get_seconds_delay");
        ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "properties"), "", "get_properties");
    }

   public:
    /// Create a RiveEvent from a rive::EventReport
    static Ref<RiveEvent> from_report(const rive::EventReport& report) {
        Ref<RiveEvent> event = memnew(RiveEvent);

        rive::Event* rive_event = report.event();
        if (!rive_event) {
            return event;
        }

        // Get event name
        event->name = String(rive_event->name().c_str());
        event->seconds_delay = report.secondsDelay();

        // Extract custom properties from the event
        // Event inherits from CustomPropertyGroup which has CustomPropertyContainer
        // The properties are stored in the event's children
        const auto& children = rive_event->children();
        for (auto* child : children) {
            if (!child) continue;

            // Check if this child is a CustomProperty
            if (auto* bool_prop = child->as<rive::CustomPropertyBoolean>()) {
                event->properties[String(bool_prop->name().c_str())] = bool_prop->propertyValue();
            } else if (auto* num_prop = child->as<rive::CustomPropertyNumber>()) {
                event->properties[String(num_prop->name().c_str())] = num_prop->propertyValue();
            } else if (auto* str_prop = child->as<rive::CustomPropertyString>()) {
                event->properties[String(str_prop->name().c_str())] = String(str_prop->propertyValue().c_str());
            }
        }

        return event;
    }

    RiveEvent() {}

    String get_name() const {
        return name;
    }

    float get_seconds_delay() const {
        return seconds_delay;
    }

    Dictionary get_properties() const {
        return properties;
    }

    Variant get_property(const String& prop_name) const {
        if (properties.has(prop_name)) {
            return properties[prop_name];
        }
        return Variant();
    }

    bool has_property(const String& prop_name) const {
        return properties.has(prop_name);
    }

    /* Overrides */

    String _to_string() const {
        Dictionary format_args;
        format_args["cls"] = get_class_static();
        format_args["name"] = name;
        format_args["prop_count"] = properties.size();
        return String("{cls}({name}, {prop_count} properties)").format(format_args);
    }
};

}  // namespace godot

#endif
