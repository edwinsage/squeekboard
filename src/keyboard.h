#ifndef __KEYBOARD_H
#define __KYBOARD_H

#include "stdbool.h"
#include "inttypes.h"

struct squeek_key;

struct squeek_key *squeek_key_new(uint32_t keycode);
void squeek_key_free(struct squeek_key *key);
void squeek_key_add_symbol(struct squeek_key* key,
                           const char *element_name,
                           const char *text, uint32_t keyval,
                           const char *label, const char *icon,
                           const char *tooltip);
uint32_t squeek_key_is_pressed(struct squeek_key *key);
void squeek_key_set_pressed(struct squeek_key *key, uint32_t pressed);
struct squeek_symbol *squeek_key_get_symbol(struct squeek_key* key, uint32_t level);
#endif
