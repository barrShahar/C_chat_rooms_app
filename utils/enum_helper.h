#ifndef ENUM_HELPER_H
#define ENUM_HELPER_H

// Helper to generate the enum fields
#define GENERATE_ENUM_FIELD(name, assign) name assign,

// Helper to generate the switch cases
#define GENERATE_STRING_CASE(name, assign) case name: return #name;

// Master Macro to build the Enum Type
#define DEFINE_ENUM(enum_name, table_name) \
    typedef enum enum_name { \
        table_name(GENERATE_ENUM_FIELD) \
    } enum_name;

// Master Macro to build the To-String Function (Now with static inline!)
#define DEFINE_ENUM_TO_STRING(enum_name, table_name) \
    static inline const char* enum_name##_toString(enum_name value) { \
        switch (value) { \
            table_name(GENERATE_STRING_CASE) \
            default: return "UNKNOWN_" #enum_name; \
        } \
    }

#endif // ENUM_HELPER_H