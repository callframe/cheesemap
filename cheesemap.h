#include <stdint.h>

#if !defined(__GNUC__) && !defined(__clang__)
#error "cheesemap requires a GNU-compatible compiler"
#endif

/** @defgroup cm_types Internal type aliases
 *  @brief Portable stdint aliases used throughout cheesemap.
 *  @internal
 *  @{
 */
typedef uint8_t cm_u8;
typedef uint32_t cm_u32;
typedef uint64_t cm_u64;

#if UINTPTR_MAX == UINT64_MAX
typedef cm_u64 cm_usize; /**< Pointer-sized unsigned integer (64-bit). */
#elif UINTPTR_MAX == UINT32_MAX
typedef cm_u32 cm_usize; /**< Pointer-sized unsigned integer (32-bit). */
#else
#error "unsupported uintptr_t width"
#endif
/** @} */

/** @defgroup cm_capabilities Hashmap capabilities and types
 *  @brief Constants and types describing cheesemap's capabilities
 *  @internal
 *  @{
 */

/** @} */

extern cm_u8* CM_CTRL_EMPTY[];

/** @internal Map struct type reference. @param _name Map name. */
#define _cm_type(_name) struct _name

/** @internal Generates a namespaced member identifier. @param _name Map name. @param _member Member name. */
#define _cm_member(_name, _member) _name##_##_member

/** @internal Entry type reference. @param _name Pair name.*/
#define _cm_entry_type(_name) struct _name##_entry

/** @internal Generate Entry struct. @param _name Pair name. @param _k Key type. @param _v Value type. */
#define _cm_entry(_name, _k, _v) \
    _cm_entry_type(_name)        \
    {                            \
        _k k;                    \
        _v v;                    \
    };

/**
 * @brief Declares a strongly-typed hash map.
 *
 * @param[in] _name    Name of the map type to generate
 * @param[in] _k       Key type
 * @param[in] _v       Value type
 */
#define cheesemap(_name, _k, _v) \
    _cm_entry(_name, _k, _v);    \
    _cm_type(_name)              \
    {                            \
        cm_u8* ctrl;             \
        cm_usize growth_left;    \
        cm_usize count;          \
        cm_usize cap_mask;       \
    };                           \
    static inline _cm_type(_name) _cm_member(_name, new)(void) { return (_cm_type(_name)) { 0 }; }
