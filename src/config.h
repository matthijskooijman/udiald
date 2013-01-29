#ifndef UDIALD_CONFIG_H_
#define UDIALD_CONFIG_H_

#include <libubox/list.h>
#include "ucix.h"

static inline char* udiald_config_get(struct udiald_state *s, const char *key) {
	return ucix_get_option(s->uci, s->uciname, s->networkname, key);
}

static inline int udiald_config_get_int
(struct udiald_state *s, const char *key, int def) {
	return ucix_get_option_int(s->uci, s->uciname, s->networkname, key, def);
}

static inline int udiald_config_get_list
(struct udiald_state *s, const char *key, struct list_head *list) {
	return ucix_get_option_list(s->uci, s->uciname, s->networkname, key, list);
}

static inline void udiald_config_revert(struct udiald_state *s, const char *key) {
	ucix_revert(s->uci, s->uciname, s->networkname, key);
}

static inline void udiald_config_set(struct udiald_state *s, const char *key, const char *val) {
	ucix_add_option(s->uci, s->uciname, s->networkname, key, val);
}

static inline void udiald_config_set_int(struct udiald_state *s, const char *key, int val) {
	ucix_add_option_int(s->uci, s->uciname, s->networkname, key, val);
}

static inline void udiald_config_append(struct udiald_state *s, const char *key, const char *val) {
	ucix_add_list_single(s->uci, s->uciname, s->networkname, key, val);
}

#endif /* UDIALD_CONFIG_H_ */
