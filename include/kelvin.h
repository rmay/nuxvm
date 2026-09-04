#ifndef KELVIN_H
#define KELVIN_H

#include <stdint.h>
#include <stddef.h>

/* Kelvin versioning -- see AGENTS.md for the spec and the cooldown log.
 *
 * Versions count DOWN. A component only ever gets colder (more final), and 0
 * is permanently frozen. The rule with teeth is rule 5: a supporting
 * component must always be strictly colder than what it supports. The
 * platform supports the guest, so the platform's number is the FLOOR for
 * anything built on it -- a guest may be as hot as it likes, but never
 * colder than the thing that has to run it.
 *
 * These two constants are the single source of truth. AGENTS.md quotes them;
 * if you cool one, cool it here and record why in the cooldown log. */

#define NUX_KELVIN       300000  /* Nux opcodes + VM implementation */
#define CLOISTER_KELVIN  399000  /* everything else: VFS devices, Lux libs, apps */

/* The platform's own two components must satisfy rule 5 against each other:
 * Nux supports Cloister, so Nux must be strictly colder. Getting this
 * backwards would make every guest check below meaningless, so catch it at
 * build time rather than trusting the comment above. */
typedef char kelvin_platform_is_well_ordered[(NUX_KELVIN < CLOISTER_KELVIN) ? 1 : -1];

/* Returns NULL if `version` is a legal guest version on this platform, or a
 * short explanation of which rule it breaks. The caller supplies the file
 * name and the number; this supplies the reason. */
static inline const char* kelvin_reject_reason(int32_t version) {
    if (version < 0) {
        /* Rule 1: a version SHALL be a nonnegative integer. */
        return "a Kelvin version must be a nonnegative integer";
    }
    if (version == 0) {
        /* Rule 3: 0 is absolute zero and may never be released again. Rule 5
         * also makes it unreachable here -- nothing can support it, since no
         * platform version is strictly colder than 0. */
        return "0K is absolute zero: permanently frozen, and nothing colder "
               "can exist to support it";
    }
    if (version < CLOISTER_KELVIN) {
        /* Rule 5. Colder means more final, so a guest below the platform is
         * asking to be supported by something that has not cooled far enough
         * to exist yet. This is the check that catches a ROM built against a
         * contract this host does not implement. */
        return "colder than the platform, which must support it (rule 5)";
    }
    return NULL;
}

#endif
